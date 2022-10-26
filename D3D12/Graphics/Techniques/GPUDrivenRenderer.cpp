#include "stdafx.h"
#include "GPUDrivenRenderer.h"
#include "../RHI/Graphics.h"
#include "../RHI/PipelineState.h"
#include "../RHI/RootSignature.h"
#include "../RenderGraph/RenderGraph.h"
#include "../Profiler.h"
#include "../SceneView.h"
#include "../Mesh.h"
#include "Core/ConsoleVariables.h"

#define A_CPU 1
#include "ffx_a.h"
#include "ffx_spd.h"

namespace Tweakables
{
	ConsoleVariable CullDebugStats("r.CullingStats", false);

	constexpr uint32 MaxNumMeshlets = 1 << 20u;
	constexpr uint32 MaxNumInstances = 1 << 14u;
}

GPUDrivenRenderer::GPUDrivenRenderer(GraphicsDevice* pDevice)
{
	m_pCommonRS = new RootSignature(pDevice);
	m_pCommonRS->AddRootConstants(0, 8);
	m_pCommonRS->AddConstantBufferView(100);
	m_pCommonRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 14);
	m_pCommonRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 6);
	m_pCommonRS->Finalize("Common");

	ShaderDefineHelper defines;
	defines.Set("OCCLUSION_FIRST_PASS", true);
	defines.Set("MAX_NUM_MESHLETS", Tweakables::MaxNumMeshlets);
	defines.Set("MAX_NUM_INSTANCES", Tweakables::MaxNumInstances);
	
	PipelineStateInitializer psoDesc;
	psoDesc.SetRootSignature(m_pCommonRS);
	psoDesc.SetAmplificationShader("MeshletCull.hlsl", "CullAndDrawMeshletsAS", *defines);
	psoDesc.SetMeshShader("MeshletCull.hlsl", "MSMain", *defines);
	psoDesc.SetPixelShader("MeshletCull.hlsl", "PSMain", *defines);
	psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
	psoDesc.SetRenderTargetFormats(ResourceFormat::R32_UINT, ResourceFormat::D32_FLOAT, 1);
	psoDesc.SetName("Visibility Rendering");
	m_pCullAndDrawPSO[0] = pDevice->CreatePipeline(psoDesc);

	defines.Set("OCCLUSION_FIRST_PASS", false);
	psoDesc.SetAmplificationShader("MeshletCull.hlsl", "CullAndDrawMeshletsAS", *defines);
	m_pCullAndDrawPSO[1] = pDevice->CreatePipeline(psoDesc);

	m_pBuildCullArgsPSO = pDevice->CreateComputePipeline(m_pCommonRS, "MeshletCull.hlsl", "BuildInstanceCullIndirectArgs", *defines);

	defines.Set("OCCLUSION_FIRST_PASS", true);
	m_pCullInstancesPSO[0] = pDevice->CreateComputePipeline(m_pCommonRS, "MeshletCull.hlsl", "CullInstancesCS", *defines);
	m_pBuildDrawArgsPSO[0] = pDevice->CreateComputePipeline(m_pCommonRS, "MeshletCull.hlsl", "BuildMeshShaderIndirectArgs", *defines);

	defines.Set("OCCLUSION_FIRST_PASS", false);
	m_pCullInstancesPSO[1] =	pDevice->CreateComputePipeline(m_pCommonRS, "MeshletCull.hlsl", "CullInstancesCS", *defines);
	m_pBuildDrawArgsPSO[1] =	pDevice->CreateComputePipeline(m_pCommonRS, "MeshletCull.hlsl", "BuildMeshShaderIndirectArgs", *defines);

	m_pPrintStatsPSO =			pDevice->CreateComputePipeline(m_pCommonRS, "MeshletCull.hlsl", "PrintStatsCS", *defines);

	pDevice->GetShaderManager()->AddIncludeDir("External/SPD/");
	m_pHZBInitializePSO =		pDevice->CreateComputePipeline(m_pCommonRS, "HZB.hlsl", "HZBInitCS");
	m_pHZBCreatePSO =			pDevice->CreateComputePipeline(m_pCommonRS, "HZB.hlsl", "HZBCreateCS");
}

void GPUDrivenRenderer::Render(RGGraph& graph, const SceneView* pView, RGTexture* pDepth, RGTexture** pOutVisibilityBuffer, RGTexture** pOutHZB, RefCountPtr<Texture>* pHZBExport, RGBuffer** pOutMeshletCandidates)
{
	RG_GRAPH_SCOPE("Rasterize (Visibility Buffer)", graph);

	TextureDesc depthDesc = pDepth->GetDesc();
	RGTexture* pHZB = InitHZB(graph, depthDesc.Size2D(), pHZBExport);
	RGTexture* pVisibilityBuffer = graph.CreateTexture("Visibility", TextureDesc::CreateRenderTarget(depthDesc.Width, depthDesc.Height, ResourceFormat::R32_UINT));

	constexpr uint32 maxNumInstances = Tweakables::MaxNumInstances;
	constexpr uint32 maxNumMeshlets = Tweakables::MaxNumMeshlets;

#if 0
	uint32 numMeshlets = 0;
	for (const Batch& b : pView->Batches)
	{
		numMeshlets += b.pMesh->NumMeshlets;
	}
	check(pView->Batches.size() <= maxNumInstances);
	check(numMeshlets <= maxNumMeshlets);
#endif

	RGBuffer* pMeshletCandidates =			graph.CreateBuffer("GPURender.MeshletCandidates", BufferDesc::CreateStructured(maxNumMeshlets, sizeof(uint32) * 2));
	RGBuffer* pMeshletCandidatesCounter =	graph.CreateBuffer("GPURender.MeshletCandidates.Counter", BufferDesc::CreateTyped(3, ResourceFormat::R32_UINT));
	RGBuffer* pOccludedInstances =			graph.CreateBuffer("GPURender.OccludedInstances", BufferDesc::CreateStructured(maxNumInstances, sizeof(uint32)));
	RGBuffer* pOccludedInstancesCounter =	graph.CreateBuffer("GPURender.OccludedInstances.Counter", BufferDesc::CreateTyped(1, ResourceFormat::R32_UINT));

	{
		RG_GRAPH_SCOPE("Phase 1", graph);

		graph.AddPass("Clear Counters", RGPassFlag::Compute)
			.Write({ pMeshletCandidatesCounter, pOccludedInstancesCounter })
			.Bind([=](CommandContext& context)
				{
					context.ClearUavUInt(pMeshletCandidatesCounter->Get());
					context.ClearUavUInt(pOccludedInstancesCounter->Get());
					context.InsertUavBarrier();
				});

		graph.AddPass("Cull Instances", RGPassFlag::Compute)
			.Read(pHZB)
			.Write({ pMeshletCandidates, pMeshletCandidatesCounter, pOccludedInstances, pOccludedInstancesCounter })
			.Bind([=](CommandContext& context)
				{
					context.SetComputeRootSignature(m_pCommonRS);
					context.SetPipelineState(m_pCullInstancesPSO[0]);

					context.SetRootCBV(1, Renderer::GetViewUniforms(pView));
					context.BindResources(2, {
						pMeshletCandidates->Get()->GetUAV(),
						pMeshletCandidatesCounter->Get()->GetUAV(),
						pOccludedInstances->Get()->GetUAV(),
						pOccludedInstancesCounter->Get()->GetUAV(),
						});
					context.BindResources(3, pHZB->Get()->GetSRV(), 3);
					context.Dispatch(ComputeUtils::GetNumThreadGroups((uint32)pView->Batches.size(), 64));
				});

		RGBuffer* pDispatchMeshBuffer = graph.CreateBuffer("GPURender.DispatchMeshArgs", BufferDesc::CreateIndirectArguments<D3D12_DISPATCH_MESH_ARGUMENTS>(1));
		graph.AddPass("Build DispatchMesh Arguments", RGPassFlag::Compute)
			.Read(pMeshletCandidatesCounter)
			.Write(pDispatchMeshBuffer)
			.Bind([=](CommandContext& context)
				{
					context.SetComputeRootSignature(m_pCommonRS);
					context.SetPipelineState(m_pBuildDrawArgsPSO[0]);

					context.BindResources(2, pDispatchMeshBuffer->Get()->GetUAV());
					context.BindResources(3, pMeshletCandidatesCounter->Get()->GetSRV(), 1);
					context.Dispatch(1);
				});

		graph.AddPass("Cull and Draw Meshlets", RGPassFlag::Raster)
			.Read({ pDispatchMeshBuffer })
			.Read({ pHZB })
			.Write({ pMeshletCandidates, pMeshletCandidatesCounter })
			.DepthStencil(pDepth, RenderTargetLoadAction::Clear, true)
			.RenderTarget(pVisibilityBuffer, RenderTargetLoadAction::DontCare)
			.Bind([=](CommandContext& context)
				{
					context.SetGraphicsRootSignature(m_pCommonRS);
					context.SetPipelineState(m_pCullAndDrawPSO[0]);

					context.SetRootCBV(1, Renderer::GetViewUniforms(pView));
					context.BindResources(2, {
						pMeshletCandidates->Get()->GetUAV(),
						pMeshletCandidatesCounter->Get()->GetUAV(),
						});
					context.BindResources(3, {
						pHZB->Get()->GetSRV(),
						}, 3);
					context.ExecuteIndirect(GraphicsCommon::pIndirectDispatchMeshSignature, 1, pDispatchMeshBuffer->Get());
				});

		BuildHZB(graph, pDepth, pHZB);
	}
	{
		RG_GRAPH_SCOPE("Phase 2", graph);

		RGBuffer* pDispatchBuffer = graph.CreateBuffer("GPURender.DispatchArgs", BufferDesc::CreateIndirectArguments<D3D12_DISPATCH_ARGUMENTS>(1));
		graph.AddPass("Build Instance Cull Arguments", RGPassFlag::Compute)
			.Read({ pOccludedInstancesCounter })
			.Write({ pDispatchBuffer })
			.Bind([=](CommandContext& context)
				{
					context.SetComputeRootSignature(m_pCommonRS);
					context.SetPipelineState(m_pBuildCullArgsPSO);

					context.BindResources(2, pDispatchBuffer->Get()->GetUAV());
					context.BindResources(3, pOccludedInstancesCounter->Get()->GetSRV(), 2);
					context.Dispatch(1);
				});

		graph.AddPass("Cull Instances", RGPassFlag::Compute)
			.Read(pHZB)
			.Read({ pOccludedInstances, pOccludedInstancesCounter, pDispatchBuffer })
			.Write({ pMeshletCandidates, pMeshletCandidatesCounter })
			.Bind([=](CommandContext& context)
				{
					context.SetComputeRootSignature(m_pCommonRS);
					context.SetPipelineState(m_pCullInstancesPSO[1]);

					context.SetRootCBV(1, Renderer::GetViewUniforms(pView));
					context.BindResources(2, {
						pMeshletCandidates->Get()->GetUAV(),
						pMeshletCandidatesCounter->Get()->GetUAV(),
						});
					context.BindResources(3, {
						pOccludedInstances->Get()->GetSRV(),
						pOccludedInstancesCounter->Get()->GetSRV(),
						pOccludedInstancesCounter->Get()->GetSRV(),
						pHZB->Get()->GetSRV(),
						});

					context.ExecuteIndirect(GraphicsCommon::pIndirectDispatchSignature, 1, pDispatchBuffer->Get());
				});


		RGBuffer* pDispatchMeshBuffer = graph.CreateBuffer("GPURender.DispatchMeshArgs", BufferDesc::CreateIndirectArguments<D3D12_DISPATCH_MESH_ARGUMENTS>(1));
		graph.AddPass("Build DispatchMesh Arguments", RGPassFlag::Compute)
			.Read(pMeshletCandidatesCounter)
			.Write(pDispatchMeshBuffer)
			.Bind([=](CommandContext& context)
				{
					context.SetComputeRootSignature(m_pCommonRS);
					context.SetPipelineState(m_pBuildDrawArgsPSO[1]);

					context.BindResources(2, pDispatchMeshBuffer->Get()->GetUAV());
					context.BindResources(3, pMeshletCandidatesCounter->Get()->GetSRV(), 1);
					context.Dispatch(1);
				});

		graph.AddPass("Cull and Draw Meshlets", RGPassFlag::Raster)
			.Read(pHZB)
			.Write({ pMeshletCandidates, pMeshletCandidatesCounter })
			.Read({ pDispatchMeshBuffer })
			.DepthStencil(pDepth, RenderTargetLoadAction::Load, true)
			.RenderTarget(pVisibilityBuffer, RenderTargetLoadAction::Load)
			.Bind([=](CommandContext& context)
				{
					context.SetGraphicsRootSignature(m_pCommonRS);
					context.SetPipelineState(m_pCullAndDrawPSO[1]);

					context.SetRootCBV(1, Renderer::GetViewUniforms(pView));
					context.BindResources(2, {
						pMeshletCandidates->Get()->GetUAV(),
						pMeshletCandidatesCounter->Get()->GetUAV(),
						});
					context.BindResources(3, {
						pHZB->Get()->GetSRV(),
						}, 3);
					context.ExecuteIndirect(GraphicsCommon::pIndirectDispatchMeshSignature, 1, pDispatchMeshBuffer->Get());
				});

		BuildHZB(graph, pDepth, pHZB);
	}

	if (Tweakables::CullDebugStats)
	{
		graph.AddPass("Print Stats", RGPassFlag::Compute)
			.Read({ pOccludedInstancesCounter, pMeshletCandidatesCounter })
			.Bind([=](CommandContext& context)
				{
					context.SetComputeRootSignature(m_pCommonRS);
					context.SetPipelineState(m_pPrintStatsPSO);

					context.SetRootCBV(1, Renderer::GetViewUniforms(pView));
					context.BindResources(3, {
						pMeshletCandidatesCounter->Get()->GetSRV(),
						pOccludedInstancesCounter->Get()->GetSRV(),
						}, 1);
					context.Dispatch(1);
				});
	}

	*pOutVisibilityBuffer = pVisibilityBuffer;
	*pOutHZB = pHZB;
	*pOutMeshletCandidates = pMeshletCandidates;
}

RGTexture* GPUDrivenRenderer::InitHZB(RGGraph& graph, const Vector2i& viewDimensions, RefCountPtr<Texture>* pExportTarget) const
{
	RGTexture* pHZB = nullptr;
	if (pExportTarget && *pExportTarget)
		pHZB = graph.TryImportTexture(*pExportTarget);

	Vector2i hzbDimensions;
	hzbDimensions.x = Math::Max(Math::NextPowerOfTwo(viewDimensions.x) >> 1u, 1u);
	hzbDimensions.y = Math::Max(Math::NextPowerOfTwo(viewDimensions.y) >> 1u, 1u);
	uint32 numMips = (uint32)Math::Floor(log2f((float)Math::Max(hzbDimensions.x, hzbDimensions.y)));
	TextureDesc desc = TextureDesc::Create2D(hzbDimensions.x, hzbDimensions.y, ResourceFormat::R16_FLOAT, TextureFlag::UnorderedAccess, 1, numMips);

	if (!pHZB || pHZB->GetDesc() != desc)
	{
		pHZB = graph.CreateTexture("HZB", desc);
		if (pExportTarget)
		{
			graph.ExportTexture(pHZB, pExportTarget);
		}
	}
	return pHZB;
}

void GPUDrivenRenderer::BuildHZB(RGGraph& graph, RGTexture* pDepth, RGTexture* pHZB)
{
	RG_GRAPH_SCOPE("HZB", graph);

	const Vector2i hzbDimensions = pHZB->GetDesc().Size2D();

	graph.AddPass("HZB Create", RGPassFlag::Compute)
		.Read(pDepth)
		.Write(pHZB)
		.Bind([=](CommandContext& context)
			{
				context.SetComputeRootSignature(m_pCommonRS);
				context.SetPipelineState(m_pHZBInitializePSO);

				struct
				{
					Vector2 DimensionsInv;
				} parameters;

				parameters.DimensionsInv = Vector2(1.0f / hzbDimensions.x, 1.0f / hzbDimensions.y);
				context.SetRootConstants(0, parameters);
				context.BindResources(2, pHZB->Get()->GetUAV());
				context.BindResources(3, pDepth->Get()->GetSRV());
				context.Dispatch(ComputeUtils::GetNumThreadGroups(hzbDimensions.x, 16, hzbDimensions.y, 16));
			});

	RGBuffer* pSPDCounter = graph.CreateBuffer("SPD Counter", BufferDesc::CreateTyped(1, ResourceFormat::R32_UINT));

	graph.AddPass("HZB Mips", RGPassFlag::Compute)
		.Write({ pHZB, pSPDCounter })
		.Bind([=](CommandContext& context)
			{
				context.ClearUavUInt(pSPDCounter->Get());
				context.InsertUavBarrier();

				context.SetComputeRootSignature(m_pCommonRS);
				context.SetPipelineState(m_pHZBCreatePSO);

				varAU2(dispatchThreadGroupCountXY);
				varAU2(workGroupOffset);
				varAU2(numWorkGroupsAndMips);
				varAU4(rectInfo) = initAU4(0, 0, (uint32)hzbDimensions.x, (uint32)hzbDimensions.y);

				SpdSetup(
					dispatchThreadGroupCountXY,
					workGroupOffset,
					numWorkGroupsAndMips,
					rectInfo,
					pHZB->GetDesc().Mips - 1);

				struct
				{
					uint32 NumMips;
					uint32 NumWorkGroups;
					Vector2u WorkGroupOffset;
				} parameters;
				parameters.NumMips = numWorkGroupsAndMips[1];
				parameters.NumWorkGroups = numWorkGroupsAndMips[0];
				parameters.WorkGroupOffset.x = workGroupOffset[0];
				parameters.WorkGroupOffset.y = workGroupOffset[1];

				context.SetRootConstants(0, parameters);
				uint32 uavIndex = 0;
				context.BindResources(2, pSPDCounter->Get()->GetUAV(), uavIndex++);
				context.BindResources(2, pHZB->Get()->GetUAV(), uavIndex++);
				for (uint8 mipIndex = 1; mipIndex < pHZB->GetDesc().Mips; ++mipIndex)
				{
					context.BindResources(2, context.GetParent()->CreateUAV(pHZB->Get(), TextureUAVDesc(mipIndex)).Get(), uavIndex++);
				}
				context.Dispatch(dispatchThreadGroupCountXY[0], dispatchThreadGroupCountXY[1]);
			});
}
