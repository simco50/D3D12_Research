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
#include "SPD/ffx_a.h"
#include "SPD/ffx_spd.h"

namespace Tweakables
{
	constexpr uint32 MaxNumMeshlets = 1 << 20u;
	constexpr uint32 MaxNumInstances = 1 << 14u;
}

GPUDrivenRenderer::GPUDrivenRenderer(GraphicsDevice* pDevice)
{
	if (!pDevice->GetCapabilities().SupportsMeshShading())
		return;

	m_pCommonRS = new RootSignature(pDevice);
	m_pCommonRS->AddRootConstants(0, 8);
	m_pCommonRS->AddConstantBufferView(100);
	m_pCommonRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 6);
	m_pCommonRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 6);
	m_pCommonRS->Finalize("Common");

	ShaderDefineHelper defines;
	defines.Set("MAX_NUM_MESHLETS", Tweakables::MaxNumMeshlets);
	defines.Set("MAX_NUM_INSTANCES", Tweakables::MaxNumInstances);
	
	m_pBuildCullArgsPSO =			pDevice->CreateComputePipeline(m_pCommonRS, "MeshletCull.hlsl", "BuildInstanceCullIndirectArgs", *defines);
	m_pClearUAVsPSO =				pDevice->CreateComputePipeline(m_pCommonRS, "MeshletCull.hlsl", "ClearUAVs", *defines);

	PipelineStateInitializer psoDesc;
	psoDesc.SetRootSignature(m_pCommonRS);
	psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
	psoDesc.SetRenderTargetFormats(ResourceFormat::R32_UINT, GraphicsCommon::DepthStencilFormat, 1);
	psoDesc.SetName("Visibility Rendering");

	// Permutation without alpha masking
	defines.Set("ALPHA_MASK", false);
	psoDesc.SetMeshShader("MeshletCull.hlsl", "MSMain", *defines);
	psoDesc.SetPixelShader("MeshletCull.hlsl", "PSMain", *defines);
	m_pDrawMeshletsPSO[0] =			pDevice->CreatePipeline(psoDesc);
	// Permutation with alpha masking
	defines.Set("ALPHA_MASK", true);
	psoDesc.SetCullMode(D3D12_CULL_MODE_NONE);
	psoDesc.SetMeshShader("MeshletCull.hlsl", "MSMain", *defines);
	psoDesc.SetPixelShader("MeshletCull.hlsl", "PSMain", *defines);
	m_pDrawMeshletsPSO[1] =			pDevice->CreatePipeline(psoDesc);

	defines.Set("OCCLUSION_FIRST_PASS", true);
	m_pBuildMeshletCullArgsPSO[0] = pDevice->CreateComputePipeline(m_pCommonRS, "MeshletCull.hlsl", "BuildMeshletCullIndirectArgs", *defines);
	m_pCullInstancesPSO[0] =		pDevice->CreateComputePipeline(m_pCommonRS, "MeshletCull.hlsl", "CullInstancesCS", *defines);
	m_pCullMeshletsPSO[0] =			pDevice->CreateComputePipeline(m_pCommonRS, "MeshletCull.hlsl", "CullMeshletsCS", *defines);

	defines.Set("OCCLUSION_FIRST_PASS", false);
	m_pBuildMeshletCullArgsPSO[1] = pDevice->CreateComputePipeline(m_pCommonRS, "MeshletCull.hlsl", "BuildMeshletCullIndirectArgs", *defines);
	m_pCullInstancesPSO[1] =		pDevice->CreateComputePipeline(m_pCommonRS, "MeshletCull.hlsl", "CullInstancesCS", *defines);
	m_pCullMeshletsPSO[1] =			pDevice->CreateComputePipeline(m_pCommonRS, "MeshletCull.hlsl", "CullMeshletsCS", *defines);

	m_pMeshletBinPrepareArgs =		pDevice->CreateComputePipeline(m_pCommonRS, "MeshletBinning.hlsl", "PrepareArgsCS", *defines);
	m_pMeshletAllocateBinRanges =	pDevice->CreateComputePipeline(m_pCommonRS, "MeshletBinning.hlsl", "AllocateBinRangesCS");
	m_pMeshletClassify =			pDevice->CreateComputePipeline(m_pCommonRS, "MeshletBinning.hlsl", "ClassifyMeshletsCS", *defines);
	m_pMeshletWriteBins =			pDevice->CreateComputePipeline(m_pCommonRS, "MeshletBinning.hlsl", "WriteBinsCS", *defines);

	m_pPrintStatsPSO =				pDevice->CreateComputePipeline(m_pCommonRS, "MeshletCull.hlsl", "PrintStatsCS", *defines);

	{
		m_pHZBRS = new RootSignature(pDevice);
		m_pHZBRS->AddRootConstants(0, 8);
		m_pHZBRS->AddConstantBufferView(100);
		m_pHZBRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 14);
		m_pHZBRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 6);
		m_pHZBRS->Finalize("HZB");

		m_pHZBInitializePSO =		pDevice->CreateComputePipeline(m_pHZBRS, "HZB.hlsl", "HZBInitCS");
		m_pHZBCreatePSO =			pDevice->CreateComputePipeline(m_pHZBRS, "HZB.hlsl", "HZBCreateCS");
	}
}

RasterContext::RasterContext(RGGraph& graph, const std::string contextString, RGTexture* pDepth, RefCountPtr<Texture>* pPreviousHZB, RasterType type)
	: ContextString(contextString), pDepth(pDepth), pPreviousHZB(pPreviousHZB), Type(type)
{
	constexpr uint32 maxNumInstances = Tweakables::MaxNumInstances;
	constexpr uint32 maxNumMeshlets = Tweakables::MaxNumMeshlets;

	struct MeshletCandidate
	{
		uint32 InstanceID;
		uint32 MeshletIndex;
	};

	const BufferDesc meshletCandidateDesc = BufferDesc::CreateStructured(maxNumMeshlets, sizeof(MeshletCandidate));
	pCandidateMeshlets = graph.Create("GPURender.CandidateMeshlets", meshletCandidateDesc);
	// 0: Num Total | 1: Num Phase 1 | 2: Num Phase 2
	pCandidateMeshletsCounter = graph.Create("GPURender.CandidateMeshlets.Counter", BufferDesc::CreateTyped(3, ResourceFormat::R32_UINT));
	pVisibleMeshlets = graph.Create("GPURender.VisibleMeshlets", meshletCandidateDesc);
	// 0: Num Phase 1 | 1: Num Phase 2
	pVisibleMeshletsCounter = graph.Create("GPURender.VisibleMeshlets.Counter", BufferDesc::CreateTyped(2, ResourceFormat::R32_UINT));

	pOccludedInstances = graph.Create("GPURender.OccludedInstances", BufferDesc::CreateStructured(maxNumInstances, sizeof(uint32)));
	pOccludedInstancesCounter = graph.Create("GPURender.OccludedInstances.Counter", BufferDesc::CreateTyped(1, ResourceFormat::R32_UINT));
}

void GPUDrivenRenderer::CullAndRasterize(RGGraph& graph, const SceneView* pView, bool isFirstPhase, const RasterContext& rasterContext, RasterResult& outResult)
{
	RGBuffer* pInstanceCullArgs = nullptr;
	if(!isFirstPhase)
	{
		pInstanceCullArgs = graph.Create("GPURender.InstanceCullArgs", BufferDesc::CreateIndirectArguments<D3D12_DISPATCH_ARGUMENTS>(1));
		graph.AddPass("Build Instance Cull Arguments", RGPassFlag::Compute)
			.Read({ rasterContext.pOccludedInstancesCounter })
			.Write({ pInstanceCullArgs })
			.Bind([=](CommandContext& context)
				{
					context.SetComputeRootSignature(m_pCommonRS);
					context.SetPipelineState(m_pBuildCullArgsPSO);

					context.BindResources(2, pInstanceCullArgs->Get()->GetUAV());
					context.BindResources(3, rasterContext.pOccludedInstancesCounter->Get()->GetSRV(), 2);
					context.Dispatch(1);
				});
	}

	RGPass& cullInstancePass = graph.AddPass("Cull Instances", RGPassFlag::Compute)
		.Read(outResult.pHZB)
		.Write({ rasterContext.pCandidateMeshlets, rasterContext.pCandidateMeshletsCounter, rasterContext.pOccludedInstances, rasterContext.pOccludedInstancesCounter })
		.Bind([=](CommandContext& context)
			{
				context.SetComputeRootSignature(m_pCommonRS);
				context.SetPipelineState(m_pCullInstancesPSO[isFirstPhase ? 0 : 1]);

				context.SetRootCBV(1, Renderer::GetViewUniforms(pView));
				context.BindResources(2, {
					rasterContext.pCandidateMeshlets->Get()->GetUAV(),
					rasterContext.pCandidateMeshletsCounter->Get()->GetUAV(),
					rasterContext.pOccludedInstances->Get()->GetUAV(),
					rasterContext.pOccludedInstancesCounter->Get()->GetUAV(),
					});
				context.BindResources(3, {
					rasterContext.pOccludedInstances->Get()->GetSRV(),
					rasterContext.pCandidateMeshletsCounter->Get()->GetSRV(),
					rasterContext.pOccludedInstancesCounter->Get()->GetSRV(),
					outResult.pHZB->Get()->GetSRV(),
					});

				if(isFirstPhase)
					context.Dispatch(ComputeUtils::GetNumThreadGroups((uint32)pView->Batches.size(), 64));
				else
					context.ExecuteIndirect(GraphicsCommon::pIndirectDispatchSignature, 1, pInstanceCullArgs->Get());
			});
	if (!isFirstPhase)
		cullInstancePass.Read(pInstanceCullArgs);

	RGBuffer* pMeshletCullArgs = graph.Create("GPURender.MeshletCullArgs", BufferDesc::CreateIndirectArguments<D3D12_DISPATCH_ARGUMENTS>(1));
	graph.AddPass("Build Meshlet Cull Arguments", RGPassFlag::Compute)
		.Read(rasterContext.pCandidateMeshletsCounter)
		.Write(pMeshletCullArgs)
		.Bind([=](CommandContext& context)
			{
				context.SetComputeRootSignature(m_pCommonRS);
				context.SetPipelineState(m_pBuildMeshletCullArgsPSO[isFirstPhase ? 0 : 1]);

				context.BindResources(2, pMeshletCullArgs->Get()->GetUAV());
				context.BindResources(3, rasterContext.pCandidateMeshletsCounter->Get()->GetSRV(), 1);
				context.Dispatch(1);
			});

	graph.AddPass("Cull Meshlets", RGPassFlag::Compute)
		.Read({ pMeshletCullArgs })
		.Read({ outResult.pHZB })
		.Write({ rasterContext.pCandidateMeshlets, rasterContext.pCandidateMeshletsCounter, rasterContext.pVisibleMeshlets, rasterContext.pVisibleMeshletsCounter })
		.Bind([=](CommandContext& context)
			{
				context.SetComputeRootSignature(m_pCommonRS);
				context.SetPipelineState(m_pCullMeshletsPSO[isFirstPhase ? 0 : 1]);

				context.SetRootCBV(1, Renderer::GetViewUniforms(pView));
				context.BindResources(2, {
					rasterContext.pCandidateMeshlets->Get()->GetUAV(),
					rasterContext.pCandidateMeshletsCounter->Get()->GetUAV(),
					rasterContext.pOccludedInstances->Get()->GetUAV(),
					rasterContext.pOccludedInstancesCounter->Get()->GetUAV(),
					rasterContext.pVisibleMeshlets->Get()->GetUAV(),
					rasterContext.pVisibleMeshletsCounter->Get()->GetUAV(),
					});
				context.BindResources(3, {
					outResult.pHZB->Get()->GetSRV(),
					}, 3);
				context.ExecuteIndirect(GraphicsCommon::pIndirectDispatchSignature, 1, pMeshletCullArgs->Get());
			});

	constexpr uint32 numBins = 2;
	RGBuffer* pMeshletCounts = graph.Create("Meshlet Counts", BufferDesc::CreateTyped(numBins, ResourceFormat::R32_UINT));
	RGBuffer* pGlobalCount = graph.Create("Global Count", BufferDesc::CreateTyped(1, ResourceFormat::R32_UINT));
	RGBuffer* pClassifyArgs = graph.Create("GPURender.ClassificationArgs", BufferDesc::CreateIndirectArguments<D3D12_DISPATCH_ARGUMENTS>(1));

	struct ClassifyParams
	{
		uint32 NumBins;
		uint32 IsSecondPhase;
	} classifyParams;
	classifyParams.NumBins = numBins;
	classifyParams.IsSecondPhase = !isFirstPhase;

	graph.AddPass("Clear UAVs", RGPassFlag::Compute)
		.Write({ pMeshletCounts, pGlobalCount, pClassifyArgs })
		.Read(rasterContext.pVisibleMeshletsCounter)
		.Bind([=](CommandContext& context)
			{
				context.SetComputeRootSignature(m_pCommonRS);
				context.SetPipelineState(m_pMeshletBinPrepareArgs);

				context.SetRootConstants(0, classifyParams);
				context.BindResources(2, {
					pMeshletCounts->Get()->GetUAV(),
					pGlobalCount->Get()->GetUAV(),
					pClassifyArgs->Get()->GetUAV(),
					});
				context.BindResources(3, {
					rasterContext.pVisibleMeshletsCounter->Get()->GetSRV(),
					}, 1);
				context.Dispatch(1);
				context.InsertUavBarrier();
			});

	graph.AddPass("Count Bins", RGPassFlag::Compute)
		.Read(pClassifyArgs)
		.Read({ rasterContext.pVisibleMeshletsCounter, rasterContext.pVisibleMeshlets })
		.Write(pMeshletCounts)
		.Bind([=](CommandContext& context)
			{
				context.SetComputeRootSignature(m_pCommonRS);
				context.SetPipelineState(m_pMeshletClassify);

				context.SetRootConstants(0, classifyParams);
				context.BindResources(2, pMeshletCounts->Get()->GetUAV());
				context.BindResources(3, {
					rasterContext.pVisibleMeshlets->Get()->GetSRV(),
					rasterContext.pVisibleMeshletsCounter->Get()->GetSRV(),
					});
				context.ExecuteIndirect(GraphicsCommon::pIndirectDispatchSignature, 1, pClassifyArgs->Get());
			});

	RGBuffer* pMeshletOffsetAndCounts = graph.Create("Meshlet offset and counts", BufferDesc::CreateStructured(numBins, sizeof(Vector4u), BufferFlag::UnorderedAccess | BufferFlag::ShaderResource | BufferFlag::IndirectArguments));

	graph.AddPass("Compute Bin Offsets", RGPassFlag::Compute)
		.Read({ pMeshletCounts })
		.Write({ pGlobalCount, pMeshletOffsetAndCounts })
		.Bind([=](CommandContext& context)
			{
				context.SetComputeRootSignature(m_pCommonRS);
				context.SetPipelineState(m_pMeshletAllocateBinRanges);

				context.SetRootConstants(0, classifyParams);
				context.BindResources(2, {
					pMeshletOffsetAndCounts->Get()->GetUAV(),
					pGlobalCount->Get()->GetUAV(),
					});
				context.BindResources(3, pMeshletCounts->Get()->GetSRV());
				context.Dispatch(ComputeUtils::GetNumThreadGroups(numBins, 64));
			});

	constexpr uint32 maxNumMeshlets = Tweakables::MaxNumMeshlets;
	RGBuffer* pBinnedMeshlets = graph.Create("BinnedMeshlets", BufferDesc::CreateStructured(maxNumMeshlets, sizeof(uint32)));

	graph.AddPass("Export Bins", RGPassFlag::Compute)
		.Read(pClassifyArgs)
		.Read({ rasterContext.pVisibleMeshletsCounter, rasterContext.pVisibleMeshlets })
		.Write({ pMeshletOffsetAndCounts, pBinnedMeshlets })
		.Bind([=](CommandContext& context)
			{
				context.SetComputeRootSignature(m_pCommonRS);
				context.SetPipelineState(m_pMeshletWriteBins);

				context.SetRootConstants(0, classifyParams);
				context.BindResources(2, {
					pMeshletOffsetAndCounts->Get()->GetUAV(),
					pBinnedMeshlets->Get()->GetUAV(),
					});
				context.BindResources(3, {
					rasterContext.pVisibleMeshlets->Get()->GetSRV(),
					rasterContext.pVisibleMeshletsCounter->Get()->GetSRV(),
					});
				context.ExecuteIndirect(GraphicsCommon::pIndirectDispatchSignature, 1, pClassifyArgs->Get());
			});

	RGPass& drawPass = graph.AddPass("Rasterize", RGPassFlag::Raster)
		.Read({ rasterContext.pVisibleMeshlets, pMeshletOffsetAndCounts, pBinnedMeshlets })
		.DepthStencil(rasterContext.pDepth, isFirstPhase ? RenderTargetLoadAction::Clear : RenderTargetLoadAction::Load, true)
		.Bind([=](CommandContext& context)
			{
				context.SetGraphicsRootSignature(m_pCommonRS);
				context.SetRootCBV(1, Renderer::GetViewUniforms(pView));
				context.BindResources(3, {
					pBinnedMeshlets->Get()->GetSRV(),
					pMeshletOffsetAndCounts->Get()->GetSRV(),
					rasterContext.pVisibleMeshlets->Get()->GetSRV(),
					}, 2);

				for (uint32 binIndex = 0; binIndex < numBins; ++binIndex)
				{
					struct
					{
						uint32 BinIndex;
					} params;
					params.BinIndex = binIndex;
					context.SetRootConstants(0, params);
					context.SetPipelineState(m_pDrawMeshletsPSO[binIndex]);
					context.ExecuteIndirect(GraphicsCommon::pIndirectDispatchMeshSignature, 1, pMeshletOffsetAndCounts->Get(), nullptr, sizeof(Vector4u) * binIndex);
				}
			});

	if (rasterContext.Type == RasterType::VisibilityBuffer)
		drawPass.RenderTarget(outResult.pVisibilityBuffer, isFirstPhase ? RenderTargetLoadAction::DontCare : RenderTargetLoadAction::Load);

	BuildHZB(graph, rasterContext.pDepth, outResult.pHZB);
}

void GPUDrivenRenderer::Render(RGGraph& graph, const SceneView* pView, const RasterContext& rasterContext, RasterResult& outResult)
{
	RG_GRAPH_SCOPE("Rasterize (Visibility Buffer)", graph);
	TextureDesc depthDesc = rasterContext.pDepth->GetDesc();

	outResult.pHZB = InitHZB(graph, depthDesc.Size2D(), rasterContext.pPreviousHZB);
	outResult.pVisibilityBuffer = nullptr;
	if (rasterContext.Type == RasterType::VisibilityBuffer)
	{
		outResult.pVisibilityBuffer = graph.Create("Visibility", TextureDesc::CreateRenderTarget(depthDesc.Width, depthDesc.Height, ResourceFormat::R32_UINT));
	}

#if 0
	uint32 numMeshlets = 0;
	for (const Batch& b : pView->Batches)
	{
		numMeshlets += b.pMesh->NumMeshlets;
	}
	check(pView->Batches.size() <= maxNumInstances);
	check(numMeshlets <= maxNumMeshlets);
#endif

	graph.AddPass("Clear UAVs", RGPassFlag::Compute)
	.Write({ rasterContext.pCandidateMeshletsCounter, rasterContext.pOccludedInstancesCounter, rasterContext.pVisibleMeshletsCounter })
	.Bind([=](CommandContext& context)
		{
			context.SetComputeRootSignature(m_pCommonRS);
			context.SetPipelineState(m_pClearUAVsPSO);

			context.BindResources(2, {
				rasterContext.pCandidateMeshletsCounter->Get()->GetUAV(),
				rasterContext.pCandidateMeshletsCounter->Get()->GetUAV(),
				rasterContext.pOccludedInstancesCounter->Get()->GetUAV(),
				rasterContext.pOccludedInstancesCounter->Get()->GetUAV(),
				rasterContext.pVisibleMeshletsCounter->Get()->GetUAV(),
				rasterContext.pVisibleMeshletsCounter->Get()->GetUAV(),
				});
			context.Dispatch(1);
			context.InsertUavBarrier();
		});

	{
		RG_GRAPH_SCOPE("Phase 1", graph);
		CullAndRasterize(graph, pView, true, rasterContext, outResult);
	}
	{
		RG_GRAPH_SCOPE("Phase 2", graph);
		CullAndRasterize(graph, pView, false, rasterContext, outResult);
	}

	outResult.pVisibleMeshlets = rasterContext.pVisibleMeshlets;
}

void GPUDrivenRenderer::PrintStats(RGGraph& graph, const SceneView* pView, const RasterContext& rasterContext)
{
	graph.AddPass("Print Stats", RGPassFlag::Compute)
		.Read({ rasterContext.pOccludedInstancesCounter, rasterContext.pCandidateMeshletsCounter, rasterContext.pVisibleMeshletsCounter })
		.Bind([=](CommandContext& context)
			{
				context.SetComputeRootSignature(m_pCommonRS);
				context.SetPipelineState(m_pPrintStatsPSO);

				context.SetRootCBV(1, Renderer::GetViewUniforms(pView));
				context.BindResources(3, {
					rasterContext.pCandidateMeshletsCounter->Get()->GetSRV(),
					rasterContext.pOccludedInstancesCounter->Get()->GetSRV(),
					rasterContext.pVisibleMeshletsCounter->Get()->GetSRV(),
					}, 1);
				context.Dispatch(1);
			});
}

RGTexture* GPUDrivenRenderer::InitHZB(RGGraph& graph, const Vector2u& viewDimensions, RefCountPtr<Texture>* pExportTarget) const
{
	RGTexture* pHZB = nullptr;
	if (pExportTarget && *pExportTarget)
		pHZB = graph.TryImport(*pExportTarget);

	Vector2u hzbDimensions;
	hzbDimensions.x = Math::Max(Math::NextPowerOfTwo(viewDimensions.x) >> 1u, 1u);
	hzbDimensions.y = Math::Max(Math::NextPowerOfTwo(viewDimensions.y) >> 1u, 1u);
	uint32 numMips = (uint32)Math::Floor(log2f((float)Math::Max(hzbDimensions.x, hzbDimensions.y)));
	TextureDesc desc = TextureDesc::Create2D(hzbDimensions.x, hzbDimensions.y, ResourceFormat::R16_FLOAT, TextureFlag::UnorderedAccess, 1, numMips);

	if (!pHZB || pHZB->GetDesc() != desc)
	{
		pHZB = graph.Create("HZB", desc);
		if (pExportTarget)
		{
			graph.Export(pHZB, pExportTarget);
		}
	}
	return pHZB;
}

void GPUDrivenRenderer::BuildHZB(RGGraph& graph, RGTexture* pDepth, RGTexture* pHZB)
{
	RG_GRAPH_SCOPE("HZB", graph);

	const Vector2u hzbDimensions = pHZB->GetDesc().Size2D();

	graph.AddPass("HZB Create", RGPassFlag::Compute)
		.Read(pDepth)
		.Write(pHZB)
		.Bind([=](CommandContext& context)
			{
				context.SetComputeRootSignature(m_pHZBRS);
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

	RGBuffer* pSPDCounter = graph.Create("SPD.Counter", BufferDesc::CreateTyped(1, ResourceFormat::R32_UINT));

	graph.AddPass("HZB Mips", RGPassFlag::Compute)
		.Write({ pHZB, pSPDCounter })
		.Bind([=](CommandContext& context)
			{
				context.ClearUAVu(pSPDCounter->Get());
				context.InsertUavBarrier();

				context.SetComputeRootSignature(m_pHZBRS);
				context.SetPipelineState(m_pHZBCreatePSO);

				varAU2(dispatchThreadGroupCountXY);
				varAU2(workGroupOffset);
				varAU2(numWorkGroupsAndMips);
				varAU4(rectInfo) = initAU4(0, 0, (uint32)hzbDimensions.x, (uint32)hzbDimensions.y);
				uint32 mips = pHZB->GetDesc().Mips;

				SpdSetup(
					dispatchThreadGroupCountXY,
					workGroupOffset,
					numWorkGroupsAndMips,
					rectInfo,
					mips - 1);

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
				context.BindResources(2, pHZB->Get()->GetSubResourceUAV(6), uavIndex++);
				for (uint8 mipIndex = 0; mipIndex < mips; ++mipIndex)
				{
					context.BindResources(2, pHZB->Get()->GetSubResourceUAV(mipIndex), uavIndex++);
				}
				context.Dispatch(dispatchThreadGroupCountXY[0], dispatchThreadGroupCountXY[1]);
			});
}

