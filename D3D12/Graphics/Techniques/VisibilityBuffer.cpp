#include "stdafx.h"
#include "VisibilityBuffer.h"
#include "../RHI/Graphics.h"
#include "../RHI/PipelineState.h"
#include "../RHI/RootSignature.h"
#include "../RenderGraph/RenderGraph.h"
#include "../Profiler.h"
#include "../SceneView.h"
#include "../Mesh.h"
#include "Core/ConsoleVariables.h"

namespace Tweakables
{
	extern ConsoleVariable<bool> g_GPUDrivenRenderPhase2;
}

VisibilityBuffer::VisibilityBuffer(GraphicsDevice* pDevice)
{
	m_pCommonRS = new RootSignature(pDevice);
	m_pCommonRS->AddRootConstants(0, 8);
	m_pCommonRS->AddConstantBufferView(100);
	m_pCommonRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 7);
	m_pCommonRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 6);
	m_pCommonRS->Finalize("Common");

	m_pCullInstancesPhase1PSO = pDevice->CreateComputePipeline(m_pCommonRS, "MeshletCull.hlsl", "CullInstancesCS", { "OCCLUSION_FIRST_PASS=1" });
	m_pBuildDrawArgsPhase1PSO = pDevice->CreateComputePipeline(m_pCommonRS, "MeshletCull.hlsl", "BuildMeshShaderIndirectArgs", { "OCCLUSION_FIRST_PASS=1" });

	PipelineStateInitializer psoDesc;
	psoDesc.SetRootSignature(m_pCommonRS);
	psoDesc.SetAmplificationShader("MeshletCull.hlsl", "CullAndDrawMeshletsAS", { "OCCLUSION_FIRST_PASS=1" });
	psoDesc.SetMeshShader("MeshletCull.hlsl", "MSMain");
	psoDesc.SetPixelShader("MeshletCull.hlsl", "PSMain");
	psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
	psoDesc.SetRenderTargetFormats(ResourceFormat::R32_UINT, ResourceFormat::D32_FLOAT, 1);
	psoDesc.SetName("Visibility Rendering");
	m_pCullAndDrawPhase1PSO = pDevice->CreatePipeline(psoDesc);

	psoDesc.SetAmplificationShader("MeshletCull.hlsl", "CullAndDrawMeshletsAS", { "OCCLUSION_FIRST_PASS=0" });
	m_pCullAndDrawPhase2PSO = pDevice->CreatePipeline(psoDesc);

	m_pBuildCullArgsPhase2PSO = pDevice->CreateComputePipeline(m_pCommonRS, "MeshletCull.hlsl", "BuildInstanceCullIndirectArgs", { "OCCLUSION_FIRST_PASS=0" });
	m_pCullInstancesPhase2PSO = pDevice->CreateComputePipeline(m_pCommonRS, "MeshletCull.hlsl", "CullInstancesCS", { "OCCLUSION_FIRST_PASS=0" });

	m_pHZBCreatePSO = pDevice->CreateComputePipeline(m_pCommonRS, "HZB.hlsl", "HZBCreateCS");
}

void VisibilityBuffer::BuildHZB(RGGraph& graph, RGTexture* pDepth, RGTexture** pOutHZB)
{
	RG_GRAPH_SCOPE("HZB", graph);

	const uint32 hzbMipsX = Math::Max(1u, (uint32)Math::Ceil(log2f((float)pDepth->GetDesc().Width)) - 1);
	const uint32 hzbMipsY = Math::Max(1u, (uint32)Math::Ceil(log2f((float)pDepth->GetDesc().Height)) - 1);
	const uint32 hzbMips = Math::Max(hzbMipsX, hzbMipsY);
	const Vector2i hzbDimensions(1 << hzbMipsX, 1 << hzbMipsY);
	if (*pOutHZB == nullptr)
	{
		*pOutHZB = graph.CreateTexture(Sprintf("HZB (%s)", pDepth->GetName()).c_str(), TextureDesc::Create2D(hzbDimensions.x, hzbDimensions.y, ResourceFormat::R32_FLOAT, TextureFlag::UnorderedAccess, 1, hzbMips));
	}
	RGTexture* pHZB = *pOutHZB;

	Vector2i currentDimensions = hzbDimensions;

	struct Parameters
	{
		Vector2 DimensionsInv;
	};

	graph.AddPass("HZB Create", RGPassFlag::Compute)
		.Read(pDepth)
		.Write(pHZB)
		.Bind([=](CommandContext& context)
			{
				context.SetComputeRootSignature(m_pCommonRS);
				context.SetPipelineState(m_pHZBCreatePSO);

				Parameters parameters;
				parameters.DimensionsInv = Vector2(1.0f / currentDimensions.x, 1.0f / currentDimensions.y);
				context.SetRootConstants(0, parameters);
				context.BindResources(2, pHZB->Get()->GetUAV());
				context.BindResources(3, pDepth->Get()->GetSRV());
				context.Dispatch(ComputeUtils::GetNumThreadGroups(currentDimensions.x, 16, currentDimensions.y, 16));
			});

	currentDimensions.x >>= 1;
	currentDimensions.y >>= 1;

	for (uint32 mipLevel = 1; mipLevel < hzbMips; ++mipLevel)
	{
		graph.AddPass("HZB Reduce", RGPassFlag::Compute)
			.Write(pHZB)
			.Bind([=](CommandContext& context)
				{
					context.SetComputeRootSignature(m_pCommonRS);
					context.SetPipelineState(m_pHZBCreatePSO);

					Parameters parameters;
					parameters.DimensionsInv = Vector2(1.0f / currentDimensions.x, 1.0f / currentDimensions.y);
					context.SetRootConstants(0, parameters);
					RefCountPtr<UnorderedAccessView> pUAV = context.GetParent()->CreateUAV(pHZB->Get(), TextureUAVDesc((uint8)mipLevel));
					context.BindResources(2, pUAV.Get());
					RefCountPtr<ShaderResourceView> pSRV = context.GetParent()->CreateSRV(pHZB->Get(), TextureSRVDesc((uint8)mipLevel - 1, 1));
					context.BindResources(3, pSRV.Get());
					context.Dispatch(ComputeUtils::GetNumThreadGroups(currentDimensions.x, 16, currentDimensions.y, 16));
				});

		currentDimensions.x >>= 1;
		currentDimensions.y >>= 1;
	}
}

RGTexture* VisibilityBuffer::Render(RGGraph& graph, const SceneView* pView, RGTexture* pDepth)
{
	RG_GRAPH_SCOPE("Visibility Buffer (GPU Driven)", graph);

	FloatRect viewport = pView->View.Viewport;

	RGTexture* pHZB = graph.TryImportTexture(m_pHZB);
	if (!pHZB)
		BuildHZB(graph, pDepth, &pHZB);

	RGTexture* pVisibilityBuffer = graph.CreateTexture("Visibility", TextureDesc::CreateRenderTarget((uint32)viewport.GetWidth(), (uint32)viewport.GetHeight(), ResourceFormat::R32_UINT));

	constexpr uint32 maxNumInstances = 1 << 14;
	constexpr uint32 maxNumMeshlets = 1 << 20;

	check(pView->Batches.size() <= maxNumInstances);
	uint32 numMeshlets = 0;
	for (const Batch& b : pView->Batches)
	{
		numMeshlets += b.pMesh->NumMeshlets;
	}
	check(numMeshlets <= maxNumMeshlets);

	BufferDesc counterDesc = BufferDesc::CreateTyped(1, ResourceFormat::R32_UINT);
	RGBuffer* pMeshletsToProcess = graph.CreateBuffer("Meshlets To Process", BufferDesc::CreateStructured(maxNumMeshlets, sizeof(uint32) * 2));
	RGBuffer* pMeshletsToProcessCounter = graph.CreateBuffer("Meshlets To Process - Counter", counterDesc);
	RGBuffer* pCulledMeshlets = graph.CreateBuffer("Culled Meshlets", BufferDesc::CreateStructured(maxNumMeshlets, sizeof(uint32) * 2));
	RGBuffer* pCulledMeshletsCounter = graph.CreateBuffer("Culled Meshlets - Counter", counterDesc);
	RGBuffer* pCulledInstances = graph.CreateBuffer("Culled Instances", BufferDesc::CreateStructured(maxNumInstances, sizeof(uint32)));
	RGBuffer* pCulledInstancesCounter = graph.CreateBuffer("Culled Instances - Counter", counterDesc);

	{
		RG_GRAPH_SCOPE("Phase 1", graph);

		graph.AddPass("Clear Counters", RGPassFlag::Compute)
			.Write({ pMeshletsToProcessCounter, pCulledInstancesCounter, pCulledMeshletsCounter })
			.Bind([=](CommandContext& context)
				{
					context.ClearUavUInt(pMeshletsToProcessCounter->Get());
					context.ClearUavUInt(pCulledInstancesCounter->Get());
					context.ClearUavUInt(pCulledMeshletsCounter->Get());
					context.InsertUavBarrier();
				});

		graph.AddPass("Cull Instances", RGPassFlag::Compute)
			.Read(pHZB)
			.Write({ pMeshletsToProcess, pMeshletsToProcessCounter, pCulledInstances, pCulledInstancesCounter })
			.Bind([=](CommandContext& context)
				{
					context.SetComputeRootSignature(m_pCommonRS);
					context.SetPipelineState(m_pCullInstancesPhase1PSO);

					context.SetRootCBV(1, Renderer::GetViewUniforms(pView));
					context.BindResources(2, {
						pMeshletsToProcess->Get()->GetUAV(),
						pMeshletsToProcessCounter->Get()->GetUAV(),
						pCulledInstances->Get()->GetUAV(),
						pCulledInstancesCounter->Get()->GetUAV(),
						});
					context.BindResources(3, pHZB->Get()->GetSRV(), 2);
					context.Dispatch(ComputeUtils::GetNumThreadGroups((uint32)pView->Batches.size(), 64));

					context.InsertUavBarrier();
				});

		RGBuffer* pDispatchMeshBuffer = graph.CreateBuffer("ExecuteIndirect - Draw and Cull - DispatchMesh Argument", BufferDesc::CreateIndirectArguments<D3D12_DISPATCH_MESH_ARGUMENTS>(1));
		graph.AddPass("Build DispatchMesh Arguments", RGPassFlag::Compute)
			.Read(pMeshletsToProcessCounter)
			.Write(pDispatchMeshBuffer)
			.Bind([=](CommandContext& context)
				{
					context.SetComputeRootSignature(m_pCommonRS);
					context.SetPipelineState(m_pBuildDrawArgsPhase1PSO);

					context.BindResources(2, pDispatchMeshBuffer->Get()->GetUAV());
					context.BindResources(3, pMeshletsToProcessCounter->Get()->GetSRV(), 1);
					context.Dispatch(1);

					context.InsertUavBarrier();
				});

		graph.AddPass("Cull and Draw Meshlets", RGPassFlag::Raster)
			.Read({ pMeshletsToProcess, pMeshletsToProcessCounter, pDispatchMeshBuffer })
			.Read({ pHZB })
			.Write({ pCulledMeshlets, pCulledMeshletsCounter })
			.DepthStencil(pDepth, RenderTargetLoadAction::Clear, true)
			.RenderTarget(pVisibilityBuffer, RenderTargetLoadAction::DontCare)
			.Bind([=](CommandContext& context)
				{
					context.SetGraphicsRootSignature(m_pCommonRS);
					context.SetPipelineState(m_pCullAndDrawPhase1PSO);

					context.SetRootCBV(1, Renderer::GetViewUniforms(pView));
					context.BindResources(2, {
						pCulledMeshlets->Get()->GetUAV(),
						pCulledMeshletsCounter->Get()->GetUAV(),
						}, 4);
					context.BindResources(3, {
						pMeshletsToProcess->Get()->GetSRV(),
						pMeshletsToProcessCounter->Get()->GetSRV(),
						pHZB->Get()->GetSRV(),
						});
					context.ExecuteIndirect(GraphicsCommon::pIndirectDispatchMeshSignature, 1, pDispatchMeshBuffer->Get());
				});

		BuildHZB(graph, pDepth, &pHZB);
	}

	if (Tweakables::g_GPUDrivenRenderPhase2)
	{
		RG_GRAPH_SCOPE("Phase 2", graph);

		RGBuffer* pDispatchBuffer = graph.CreateBuffer("ExecuteIndirect - Instance Culling - Dispatch Arguments", BufferDesc::CreateIndirectArguments<D3D12_DISPATCH_ARGUMENTS>(1));
		graph.AddPass("Build Instance Cull Arguments", RGPassFlag::Compute)
			.Read({ pCulledInstancesCounter })
			.Write({ pDispatchBuffer })
			.Bind([=](CommandContext& context)
				{
					context.SetComputeRootSignature(m_pCommonRS);
					context.SetPipelineState(m_pBuildCullArgsPhase2PSO);

					context.BindResources(2, pDispatchBuffer->Get()->GetUAV());
					context.BindResources(3, pCulledInstancesCounter->Get()->GetSRV(), 1);
					context.Dispatch(1);

					context.InsertUavBarrier();
				});

		graph.AddPass("Cull Instances", RGPassFlag::Compute)
			.Read(pHZB)
			.Read({ pCulledInstances, pCulledInstancesCounter, pDispatchBuffer })
			.Write({ pCulledMeshlets, pCulledMeshletsCounter })
			.Bind([=](CommandContext& context)
				{
					context.SetComputeRootSignature(m_pCommonRS);
					context.SetPipelineState(m_pCullInstancesPhase2PSO);

					context.SetRootCBV(1, Renderer::GetViewUniforms(pView));
					context.BindResources(2, {
						pCulledMeshlets->Get()->GetUAV(),
						pCulledMeshletsCounter->Get()->GetUAV(),
						});
					context.BindResources(3, {
						pCulledInstances->Get()->GetSRV(),
						pCulledInstancesCounter->Get()->GetSRV(),
						pHZB->Get()->GetSRV(),
						});

					context.ExecuteIndirect(GraphicsCommon::pIndirectDispatchSignature, 1, pDispatchBuffer->Get());

					context.InsertUavBarrier();
				});


		RGBuffer* pDispatchMeshBuffer = graph.CreateBuffer("ExecuteIndirect - Draw and Cull - DispatchMesh Argument", BufferDesc::CreateIndirectArguments<D3D12_DISPATCH_MESH_ARGUMENTS>(1));
		graph.AddPass("Build DispatchMesh Arguments", RGPassFlag::Compute)
			.Read(pCulledMeshletsCounter)
			.Write(pDispatchMeshBuffer)
			.Bind([=](CommandContext& context)
				{
					context.SetComputeRootSignature(m_pCommonRS);
					context.SetPipelineState(m_pBuildDrawArgsPhase1PSO);

					context.BindResources(2, pDispatchMeshBuffer->Get()->GetUAV());
					context.BindResources(3, pCulledMeshletsCounter->Get()->GetSRV(), 1);
					context.Dispatch(1);

					context.InsertUavBarrier();
				});

		graph.AddPass("Cull and Draw Meshlets", RGPassFlag::Raster)
			.Read(pHZB)
			.Read({ pCulledMeshlets, pCulledMeshletsCounter, pDispatchMeshBuffer })
			.DepthStencil(pDepth, RenderTargetLoadAction::Load, true)
			.RenderTarget(pVisibilityBuffer, RenderTargetLoadAction::Load)
			.Bind([=](CommandContext& context)
				{
					context.SetGraphicsRootSignature(m_pCommonRS);
					context.SetPipelineState(m_pCullAndDrawPhase2PSO);

					context.SetRootCBV(1, Renderer::GetViewUniforms(pView));
					context.BindResources(3, {
						pCulledMeshlets->Get()->GetSRV(),
						pCulledMeshletsCounter->Get()->GetSRV(),
						pHZB->Get()->GetSRV(),
						});
					context.ExecuteIndirect(GraphicsCommon::pIndirectDispatchMeshSignature, 1, pDispatchMeshBuffer->Get());
				});

		BuildHZB(graph, pDepth, &pHZB);
	}

	graph.ExportTexture(pHZB, &m_pHZB);
	return pVisibilityBuffer;
}
