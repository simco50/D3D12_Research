#include "stdafx.h"
#include "GPUDrivenRenderer.h"
#include "Core/ConsoleVariables.h"
#include "Graphics/RHI/Graphics.h"
#include "Graphics/RHI/PipelineState.h"
#include "Graphics/RHI/RootSignature.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/Profiler.h"
#include "Graphics/Mesh.h"
#include "Graphics/SceneView.h"

#define A_CPU 1
#include "SPD/ffx_a.h"
#include "SPD/ffx_spd.h"

/*
	The GPU driver renderer aims to lift the weight of frustum culling, occlusion culling, draw recording off the CPU
	and performs as much of this work as possible in parallel on the GPU.
	In order for this to work, all scene data required to render the entire scene must be accessible by the GPU at once.

	Geometry is split up into 'Meshlets', so there is a two level hierarchy of culling: Instances and Meshlets.

	This system implements the "Two Phase Occlusion Culling" algorithm presented by Sebastian Aaltonen at SIGGRAPH 2015.
	It presents an accurrate GPU-driven method of performing frustum and occlusion culling and revolves around using the
	depth buffer of the previous frame to make an initial conservative approximation of visible objects, and completes the
	missing objects in a seconday phase. This works well with the assumption that objects that were visible last frame,
	are likely to be visible in the current.

	As mentioned the system works in 2 phases:

		In Phase 1, all instances are frustum culled against the current frame's view frustum, if inside the frustum,
		we test whether the instances _was_ occluded last frame by using last frame's HZB and transforms.
		If the object is unoccluded, it gets queued to get its individual meshlets test in a similar fashion.
		If the object is occluded, it means the object was occluded last frame but it may have become visible this frame.
		These objects are queued in a second list to be re-tested in Phase 2.
		Once the same process is done for meshlets, all visible meshlets in Phase 1 are drawn with an indirect draw.
		At this point an HZB is built from the depth buffer which has all things that have been rendered in Phase 1.

		In Phase 2, the list of occluded objects from Phase 1 get retested, but this time using the HZB created in Phase 1
		and using the current frame's transforms.
		This again outputs a list of objects which were occluded last frame, but no longer are in the current frame.
		The same process is done for meshlets and all the visible meshlets are rendered with another indirect draw.
		To finish off, the HZB gets recreated with the final depth buffer, to be used by Phase 1 in the next frame.

	All visible meshlets are written to a single list in an unordered fashion. So in order to support different
	PSOs, a classification must happen in each phase which buckets each meshlet in a bin associated with a PSO.
	These bins can then be drawn successively, each with its own PSO.
*/

namespace Tweakables
{
	// ~ 1.000.000 meshlets x MeshletCandidate (8 bytes) == 8MB (x2 visible/candidate meshlets)
	constexpr uint32 MaxNumMeshlets = 1 << 20u;
	// ~ 16.000 instances x Instance (4 bytes) == 64KB
	constexpr uint32 MaxNumInstances = 1 << 14u;
}

GPUDrivenRenderer::GPUDrivenRenderer(GraphicsDevice* pDevice)
{
	if (!pDevice->GetCapabilities().SupportsMeshShading())
		return;

	m_pCommonRS = new RootSignature(pDevice);
	m_pCommonRS->AddRootConstants(0, 8);
	m_pCommonRS->AddRootCBV(100);
	m_pCommonRS->AddDescriptorTable(0, 16, D3D12_DESCRIPTOR_RANGE_TYPE_UAV);
	m_pCommonRS->AddDescriptorTable(0, 64, D3D12_DESCRIPTOR_RANGE_TYPE_SRV);
	m_pCommonRS->Finalize("Common");

	ShaderDefineHelper defines;
	defines.Set("MAX_NUM_MESHLETS", Tweakables::MaxNumMeshlets);
	defines.Set("MAX_NUM_INSTANCES", Tweakables::MaxNumInstances);
	
	m_pBuildCullArgsPSO =			pDevice->CreateComputePipeline(m_pCommonRS, "MeshletCull.hlsl", "BuildInstanceCullIndirectArgs", *defines);

	// Raster PSOs for visibility buffer
	{
		ShaderDefineHelper rasterDefines(defines);

		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pCommonRS);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		psoDesc.SetRenderTargetFormats(ResourceFormat::R32_UINT, GraphicsCommon::DepthStencilFormat, 1);
		psoDesc.SetName("Meshlet Rasterize (Visibility Buffer)");

		// Permutation without alpha masking
		rasterDefines.Set("ALPHA_MASK", false);
		rasterDefines.Set("ENABLE_DEBUG_DATA", false);
		psoDesc.SetMeshShader("MeshletRasterize.hlsl", "MSMain", *rasterDefines);
		psoDesc.SetPixelShader("MeshletRasterize.hlsl", "PSMain", *rasterDefines);
		m_pDrawMeshletsPSO[(int)PipelineBin::Opaque] = pDevice->CreatePipeline(psoDesc);
		rasterDefines.Set("ENABLE_DEBUG_DATA", true);
		psoDesc.SetPixelShader("MeshletRasterize.hlsl", "PSMain", *rasterDefines);
		m_pDrawMeshletsDebugModePSO[(int)PipelineBin::Opaque] = pDevice->CreatePipeline(psoDesc);
		// Permutation with alpha masking
		rasterDefines.Set("ALPHA_MASK", true);
		rasterDefines.Set("ENABLE_DEBUG_DATA", false);
		psoDesc.SetCullMode(D3D12_CULL_MODE_NONE);
		psoDesc.SetMeshShader("MeshletRasterize.hlsl", "MSMain", *rasterDefines);
		psoDesc.SetPixelShader("MeshletRasterize.hlsl", "PSMain", *rasterDefines);
		m_pDrawMeshletsPSO[(int)PipelineBin::AlphaMasked] = pDevice->CreatePipeline(psoDesc);
		rasterDefines.Set("ENABLE_DEBUG_DATA", true);
		psoDesc.SetPixelShader("MeshletRasterize.hlsl", "PSMain", *rasterDefines);
		m_pDrawMeshletsDebugModePSO[(int)PipelineBin::AlphaMasked] = pDevice->CreatePipeline(psoDesc);
	}

	// Raster PSOs for depth-only
	{
		ShaderDefineHelper rasterDefines(defines);
		rasterDefines.Set("DEPTH_ONLY", true);

		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pCommonRS);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		psoDesc.SetDepthOnlyTarget(GraphicsCommon::DepthStencilFormat, 1);
		psoDesc.SetDepthBias(-10, 0, -4.0f);
		psoDesc.SetCullMode(D3D12_CULL_MODE_NONE);
		psoDesc.SetName("Meshlet Rasterize (Depth Only)");

		// Permutation without alpha masking
		rasterDefines.Set("ALPHA_MASK", false);
		psoDesc.SetMeshShader("MeshletRasterize.hlsl", "MSMain", *rasterDefines);
		m_pDrawMeshletsDepthOnlyPSO[(int)PipelineBin::Opaque] = pDevice->CreatePipeline(psoDesc);
		// Permutation with alpha masking
		rasterDefines.Set("ALPHA_MASK", true);
		psoDesc.SetCullMode(D3D12_CULL_MODE_NONE);
		psoDesc.SetMeshShader("MeshletRasterize.hlsl", "MSMain", *rasterDefines);
		psoDesc.SetPixelShader("MeshletRasterize.hlsl", "PSMain", *rasterDefines);
		m_pDrawMeshletsDepthOnlyPSO[(int)PipelineBin::AlphaMasked] = pDevice->CreatePipeline(psoDesc);
	}

	// First Phase culling PSOs
	defines.Set("OCCLUSION_FIRST_PASS", true);
	m_pBuildMeshletCullArgsPSO[0] = pDevice->CreateComputePipeline(m_pCommonRS, "MeshletCull.hlsl", "BuildMeshletCullIndirectArgs", *defines);
	m_pCullInstancesPSO[0] =		pDevice->CreateComputePipeline(m_pCommonRS, "MeshletCull.hlsl", "CullInstancesCS", *defines);
	m_pCullMeshletsPSO[0] =			pDevice->CreateComputePipeline(m_pCommonRS, "MeshletCull.hlsl", "CullMeshletsCS", *defines);

	// Second Phase culling PSOs
	defines.Set("OCCLUSION_FIRST_PASS", false);
	m_pBuildMeshletCullArgsPSO[1] = pDevice->CreateComputePipeline(m_pCommonRS, "MeshletCull.hlsl", "BuildMeshletCullIndirectArgs", *defines);
	m_pCullInstancesPSO[1] =		pDevice->CreateComputePipeline(m_pCommonRS, "MeshletCull.hlsl", "CullInstancesCS", *defines);
	m_pCullMeshletsPSO[1] =			pDevice->CreateComputePipeline(m_pCommonRS, "MeshletCull.hlsl", "CullMeshletsCS", *defines);

	// No-occlusion culling PSOs
	defines.Set("OCCLUSION_CULL", false);
	defines.Set("OCCLUSION_FIRST_PASS", true);
	m_pCullInstancesNoOcclusionPSO = pDevice->CreateComputePipeline(m_pCommonRS, "MeshletCull.hlsl", "CullInstancesCS", *defines);
	m_pCullMeshletsNoOcclusionPSO = pDevice->CreateComputePipeline(m_pCommonRS, "MeshletCull.hlsl", "CullMeshletsCS", *defines);

	// Classification PSOs
	m_pMeshletBinPrepareArgs =		pDevice->CreateComputePipeline(m_pCommonRS, "MeshletBinning.hlsl", "PrepareArgsCS", *defines);
	m_pMeshletAllocateBinRanges =	pDevice->CreateComputePipeline(m_pCommonRS, "MeshletBinning.hlsl", "AllocateBinRangesCS");
	m_pMeshletClassify =			pDevice->CreateComputePipeline(m_pCommonRS, "MeshletBinning.hlsl", "ClassifyMeshletsCS", *defines);
	m_pMeshletWriteBins =			pDevice->CreateComputePipeline(m_pCommonRS, "MeshletBinning.hlsl", "WriteBinsCS", *defines);
	
	// HZB PSOs
	m_pHZBInitializePSO =			pDevice->CreateComputePipeline(m_pCommonRS, "HZB.hlsl", "HZBInitCS");
	m_pHZBCreatePSO =				pDevice->CreateComputePipeline(m_pCommonRS, "HZB.hlsl", "HZBCreateCS");

	// Debug PSOs
	m_pPrintStatsPSO =				pDevice->CreateComputePipeline(m_pCommonRS, "MeshletCull.hlsl", "PrintStatsCS", *defines);
}

RasterContext::RasterContext(RGGraph& graph, RGTexture* pDepth, RasterMode mode, RefCountPtr<Texture>* pPreviousHZB)
	: Mode(mode), pDepth(pDepth), pPreviousHZB(pPreviousHZB)
{
	/// Must be kept in sync with shader! See "VisibilityBuffer.hlsli"
	struct MeshletCandidate
	{
		uint32 InstanceID;
		uint32 MeshletIndex;
	};

	pCandidateMeshlets			= graph.Create("GPURender.CandidateMeshlets",			BufferDesc::CreateStructured(Tweakables::MaxNumMeshlets, sizeof(MeshletCandidate)));
	pVisibleMeshlets			= graph.Create("GPURender.VisibleMeshlets",				BufferDesc::CreateStructured(Tweakables::MaxNumMeshlets, sizeof(MeshletCandidate)));

	pOccludedInstances			= graph.Create("GPURender.OccludedInstances",			BufferDesc::CreateStructured(Tweakables::MaxNumInstances, sizeof(uint32)));
	pOccludedInstancesCounter	= graph.Create("GPURender.OccludedInstances.Counter",	BufferDesc::CreateTyped(1, ResourceFormat::R32_UINT));

	// 0: Num Total | 1: Num Phase 1 | 2: Num Phase 2
	pCandidateMeshletsCounter	= graph.Create("GPURender.CandidateMeshlets.Counter",	BufferDesc::CreateTyped(3, ResourceFormat::R32_UINT));
	// 0: Num Phase 1 | 1: Num Phase 2
	pVisibleMeshletsCounter		= graph.Create("GPURender.VisibleMeshlets.Counter",		BufferDesc::CreateTyped(2, ResourceFormat::R32_UINT));
}

void GPUDrivenRenderer::CullAndRasterize(RGGraph& graph, const SceneView* pView, const ViewTransform* pViewTransform, RasterPhase rasterPhase, const RasterContext& rasterContext, RasterResult& outResult)
{
	RGBuffer* pInstanceCullArgs = nullptr;

	// In Phase 1, read from the previous frame's HZB
	RGTexture* pSourceHZB = nullptr;
	if (rasterContext.EnableOcclusionCulling)
	{
		if (rasterPhase == RasterPhase::Phase1)
			pSourceHZB = graph.TryImport(*rasterContext.pPreviousHZB, GraphicsCommon::GetDefaultTexture(DefaultTexture::Black2D));
		else
			pSourceHZB = outResult.pHZB;
	}

	// PSO index to use based on current phase, if the PSO has permutations
	const int psoPhaseIndex = rasterPhase == RasterPhase::Phase1 ? 0 : 1;

	PipelineState* pCullMeshletPSO = m_pCullMeshletsPSO[psoPhaseIndex];
	PipelineState* pCullInstancePSO = m_pCullInstancesPSO[psoPhaseIndex];
	PipelineStateBinSet* pRasterPSOs = rasterContext.EnableDebug ? &m_pDrawMeshletsDebugModePSO : &m_pDrawMeshletsPSO;

	if (!rasterContext.EnableOcclusionCulling)
	{
		pCullInstancePSO = m_pCullInstancesNoOcclusionPSO;
		pCullMeshletPSO = m_pCullMeshletsNoOcclusionPSO;
	}

	if (rasterContext.Mode == RasterMode::Shadows)
		pRasterPSOs = &m_pDrawMeshletsDepthOnlyPSO;

	// In Phase 2, build the indirect arguments based on the instance culling results of Phase 1.
	// These are the list of instances which within the frustum, but were considered occluded by Phase 1.
	if(rasterPhase == RasterPhase::Phase2)
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

	// Process instances and output meshlets of each visible instance.
	// In Phase 1, also output instances which are occluded according to the previous frame's HZB, and have to be retested in Phase 2.
	// In Phase 2, outputs visible meshlets which were considered occluded before, but are not based on the updated HZB created in Phase 1.
	RGPass& cullInstancePass = graph.AddPass("Cull Instances", RGPassFlag::Compute)
		.Write({ rasterContext.pCandidateMeshlets, rasterContext.pCandidateMeshletsCounter, rasterContext.pOccludedInstances, rasterContext.pOccludedInstancesCounter })
		.Bind([=](CommandContext& context)
			{
				context.SetComputeRootSignature(m_pCommonRS);
				context.SetPipelineState(pCullInstancePSO);

				struct
				{
					Vector2u HZBDimensions;
				} params;
				params.HZBDimensions = pSourceHZB ? pSourceHZB->GetDesc().Size2D() : Vector2u(0, 0);

				context.BindRootCBV(0, params);
				context.BindRootCBV(1, Renderer::GetViewUniforms(pView, pViewTransform));
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
					});

				if (rasterContext.EnableOcclusionCulling)
					context.BindResources(3, pSourceHZB->Get()->GetSRV(), 3);

				if (rasterPhase == RasterPhase::Phase1)
					context.Dispatch(ComputeUtils::GetNumThreadGroups((uint32)pView->Batches.size(), 64));
				else
					context.ExecuteIndirect(GraphicsCommon::pIndirectDispatchSignature, 1, pInstanceCullArgs->Get());
			});
	// In Phase 2, use the indirect arguments built before.
	if (rasterPhase == RasterPhase::Phase2)
		cullInstancePass.Read(pInstanceCullArgs);
	if (rasterContext.EnableOcclusionCulling)
		cullInstancePass.Read(pSourceHZB);

	// Build indirect arguments for the next pass, based on the visible list of meshlets.
	RGBuffer* pMeshletCullArgs = graph.Create("GPURender.MeshletCullArgs", BufferDesc::CreateIndirectArguments<D3D12_DISPATCH_ARGUMENTS>(1));
	graph.AddPass("Build Meshlet Cull Arguments", RGPassFlag::Compute)
		.Read(rasterContext.pCandidateMeshletsCounter)
		.Write(pMeshletCullArgs)
		.Bind([=](CommandContext& context)
			{
				context.SetComputeRootSignature(m_pCommonRS);
				context.SetPipelineState(m_pBuildMeshletCullArgsPSO[psoPhaseIndex]);

				context.BindResources(2, pMeshletCullArgs->Get()->GetUAV());
				context.BindResources(3, rasterContext.pCandidateMeshletsCounter->Get()->GetSRV(), 1);
				context.Dispatch(1);
			});

	// Process the list of meshlets and output a list of visible meshlets.
	// In Phase 1, also output meshlets which were occluded according to the previous frame's HZB, and have to be retested in Phase 2.
	// In Phase 2, outputs visible meshlets which were considered occluded before, but are not based on the updated HZB created in Phase 1.
	RGPass& meshletCullPass = graph.AddPass("Cull Meshlets", RGPassFlag::Compute)
		.Read({ pMeshletCullArgs })
		.Write({ rasterContext.pCandidateMeshlets, rasterContext.pCandidateMeshletsCounter, rasterContext.pVisibleMeshlets, rasterContext.pVisibleMeshletsCounter })
		.Bind([=](CommandContext& context)
			{
				context.SetComputeRootSignature(m_pCommonRS);
				context.SetPipelineState(pCullMeshletPSO);

				struct
				{
					Vector2u HZBDimensions;
				} params;
				params.HZBDimensions = pSourceHZB ? pSourceHZB->GetDesc().Size2D() : Vector2u(0, 0);

				context.BindRootCBV(0, params);
				context.BindRootCBV(1, Renderer::GetViewUniforms(pView, pViewTransform));
				context.BindResources(2, {
					rasterContext.pCandidateMeshlets->Get()->GetUAV(),
					rasterContext.pCandidateMeshletsCounter->Get()->GetUAV(),
					rasterContext.pOccludedInstances->Get()->GetUAV(),
					rasterContext.pOccludedInstancesCounter->Get()->GetUAV(),
					rasterContext.pVisibleMeshlets->Get()->GetUAV(),
					rasterContext.pVisibleMeshletsCounter->Get()->GetUAV(),
					});
				if (rasterContext.EnableOcclusionCulling)
					context.BindResources(3, pSourceHZB->Get()->GetSRV(), 3);

				context.ExecuteIndirect(GraphicsCommon::pIndirectDispatchSignature, 1, pMeshletCullArgs->Get());
			});
	if (rasterContext.EnableOcclusionCulling)
		meshletCullPass.Read(pSourceHZB);
	/*
		Visible meshlets are output in a single list and in an unordered fashion.
		Each of these meshlets can want a different PSO.
		The following passes perform classification and binning based on desired PSO.
		With these bins, we build a set of indirect dispatch arguments for each PSO
		so we can switch PSOs in between each bin.

		The output of the following passes is a buffer with an 'Offset' and 'Size' of each bin,
		together with an indirection list to retrieve the actual meshlet data.
	*/

	// #todo: Hardcode number of bins. Only implemented 2 PSOs (ie. Opaque and alpha masked)
	constexpr uint32 numBins = 2;
	RGBuffer* pMeshletOffsetAndCounts = graph.Create("GPURender.Classify.MeshletOffsetAndCounts", BufferDesc::CreateStructured(numBins, sizeof(Vector4u), BufferFlag::UnorderedAccess | BufferFlag::ShaderResource | BufferFlag::IndirectArguments));
	constexpr uint32 maxNumMeshlets = Tweakables::MaxNumMeshlets;
	RGBuffer* pBinnedMeshlets = graph.Create("GPURender.Classify.BinnedMeshlets", BufferDesc::CreateStructured(maxNumMeshlets, sizeof(uint32)));

	{
		RG_GRAPH_SCOPE("Classify Shader Types", graph);

		RGBuffer* pMeshletCounts	= graph.Create("GPURender.Classify.MeshletCounts", BufferDesc::CreateTyped(numBins, ResourceFormat::R32_UINT));
		RGBuffer* pGlobalCount		= graph.Create("GPURender.Classify.GlobalCount", BufferDesc::CreateTyped(1, ResourceFormat::R32_UINT));
		RGBuffer* pClassifyArgs		= graph.Create("GPURender.Classify.Args", BufferDesc::CreateIndirectArguments<D3D12_DISPATCH_ARGUMENTS>(1));

		struct ClassifyParams
		{
			uint32 NumBins;
			uint32 IsSecondPhase;
		} classifyParams;
		classifyParams.NumBins = numBins;
		classifyParams.IsSecondPhase = rasterPhase == RasterPhase::Phase2;

		// Clear counters and initialize indirect draw arguments
		graph.AddPass("Prepare Classify", RGPassFlag::Compute)
			.Write({ pMeshletCounts, pGlobalCount, pClassifyArgs })
			.Read(rasterContext.pVisibleMeshletsCounter)
			.Bind([=](CommandContext& context)
				{
					context.SetComputeRootSignature(m_pCommonRS);
					context.SetPipelineState(m_pMeshletBinPrepareArgs);

					context.BindRootCBV(0, classifyParams);
					context.BindResources(2, {
						pMeshletCounts->Get()->GetUAV(),
						pGlobalCount->Get()->GetUAV(),
						pClassifyArgs->Get()->GetUAV(),
						});
					context.BindResources(3, {
						rasterContext.pVisibleMeshletsCounter->Get()->GetSRV(),
						}, 1);
					context.Dispatch(1);
					context.InsertUAVBarrier();
				});

		// For each meshlet, find in which bin it belongs and store how many meshlets are in each bin.
		graph.AddPass("Count Meshlets", RGPassFlag::Compute)
			.Read(pClassifyArgs)
			.Read({ rasterContext.pVisibleMeshletsCounter, rasterContext.pVisibleMeshlets })
			.Write(pMeshletCounts)
			.Bind([=](CommandContext& context)
				{
					context.SetComputeRootSignature(m_pCommonRS);
					context.SetPipelineState(m_pMeshletClassify);

					context.BindRootCBV(0, classifyParams);
					context.BindResources(2, pMeshletCounts->Get()->GetUAV());
					context.BindResources(3, {
						rasterContext.pVisibleMeshlets->Get()->GetSRV(),
						rasterContext.pVisibleMeshletsCounter->Get()->GetSRV(),
						});
					context.ExecuteIndirect(GraphicsCommon::pIndirectDispatchSignature, 1, pClassifyArgs->Get());
				});

		// Perform a prefix sum on the bin counts to retrieve the first index of each bin.
		graph.AddPass("Compute Bin Offsets", RGPassFlag::Compute)
			.Read({ pMeshletCounts })
			.Write({ pGlobalCount, pMeshletOffsetAndCounts })
			.Bind([=](CommandContext& context)
				{
					context.SetComputeRootSignature(m_pCommonRS);
					context.SetPipelineState(m_pMeshletAllocateBinRanges);

					context.BindRootCBV(0, classifyParams);
					context.BindResources(2, {
						pMeshletOffsetAndCounts->Get()->GetUAV(),
						pGlobalCount->Get()->GetUAV(),
						});
					context.BindResources(3, pMeshletCounts->Get()->GetSRV());
					context.Dispatch(ComputeUtils::GetNumThreadGroups(numBins, 64));
				});

		// Write the meshlet index of each meshlet into the appropriate bin.
		// This will serve as an indirection list to retrieve meshlets.
		graph.AddPass("Write Bins", RGPassFlag::Compute)
			.Read(pClassifyArgs)
			.Read({ rasterContext.pVisibleMeshletsCounter, rasterContext.pVisibleMeshlets })
			.Write({ pMeshletOffsetAndCounts, pBinnedMeshlets })
			.Bind([=](CommandContext& context)
				{
					context.SetComputeRootSignature(m_pCommonRS);
					context.SetPipelineState(m_pMeshletWriteBins);

					context.BindRootCBV(0, classifyParams);
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
	}

	// Finally, using the list of visible meshlets and classification data, rasterize the meshlets.
	// For each bin, we bind the associated PSO and record an indirect DispatchMesh.
	const RenderTargetLoadAction depthLoadAction = rasterPhase == RasterPhase::Phase1 ? RenderTargetLoadAction::Clear : RenderTargetLoadAction::Load;
	RGPass& drawPass = graph.AddPass("Rasterize", RGPassFlag::Raster)
		.Read({ rasterContext.pVisibleMeshlets, pMeshletOffsetAndCounts, pBinnedMeshlets })
		.Write(outResult.pDebugData)
		.DepthStencil(rasterContext.pDepth, depthLoadAction, true)
		.Bind([=](CommandContext& context)
			{
				context.SetGraphicsRootSignature(m_pCommonRS);

				context.BindRootCBV(1, Renderer::GetViewUniforms(pView, pViewTransform));
				if (outResult.pDebugData)
					context.BindResources(2, outResult.pDebugData->Get()->GetUAV());
				context.BindResources(3, {
					rasterContext.pVisibleMeshlets->Get()->GetSRV(),
					pBinnedMeshlets->Get()->GetSRV(),
					pMeshletOffsetAndCounts->Get()->GetSRV(),
					});

				static constexpr const char* PipelineBinToString[] = {
					"Opaque",
					"Alpha Masked"
				};
				static_assert(ARRAYSIZE(PipelineBinToString) == (int)PipelineBin::Count);
				
				for (uint32 binIndex = 0; binIndex < numBins; ++binIndex)
				{
					GPU_PROFILE_SCOPE(Sprintf("Raster Bin - %s", PipelineBinToString[binIndex]).c_str(), &context);

					struct
					{
						uint32 BinIndex;
					} params;
					params.BinIndex = binIndex;
					context.BindRootCBV(0, params);
					context.SetPipelineState(pRasterPSOs->at(binIndex));
					context.ExecuteIndirect(GraphicsCommon::pIndirectDispatchMeshSignature, 1, pMeshletOffsetAndCounts->Get(), nullptr, sizeof(Vector4u) * binIndex);
				}
			});

	if(outResult.pVisibilityBuffer)
		drawPass.RenderTarget(outResult.pVisibilityBuffer, rasterPhase == RasterPhase::Phase1 ? RenderTargetLoadAction::DontCare : RenderTargetLoadAction::Load);

	// Build the HZB, this HZB must be persistent across frames for this system to work.
	// In Phase 1, the HZB is built so it can be used in Phase 2 for accurrate occlusion culling.
	// In Phase 2, the HZB is built to be used by Phase 1 in the next frame.
	if (rasterContext.EnableOcclusionCulling)
		BuildHZB(graph, rasterContext.pDepth, outResult.pHZB);
}

void GPUDrivenRenderer::Render(RGGraph& graph, const SceneView* pView, const ViewTransform* pViewTransform, const RasterContext& rasterContext, RasterResult& outResult)
{
	checkf(!rasterContext.EnableOcclusionCulling || rasterContext.pPreviousHZB, "Occlusion Culling required previous frame's HZB")

	RG_GRAPH_SCOPE("Cull and Rasterize", graph);

#if _DEBUG
	// Validate that we don't have more meshlets/instances than allowed.
	uint32 numMeshlets = 0;
	for (const Batch& b : pView->Batches)
		numMeshlets += b.pMesh->NumMeshlets;
	check(pView->Batches.size() <= Tweakables::MaxNumInstances);
	check(numMeshlets <= Tweakables::MaxNumMeshlets);
#endif

	Vector2u dimensions = rasterContext.pDepth->GetDesc().Size2D();
	outResult.pHZB = nullptr;
	outResult.pVisibilityBuffer = nullptr;
	if (rasterContext.Mode == RasterMode::VisibilityBuffer)		
		outResult.pVisibilityBuffer = graph.Create("Visibility", TextureDesc::CreateRenderTarget(dimensions.x, dimensions.y, ResourceFormat::R32_UINT));

	if (rasterContext.EnableOcclusionCulling)
	{
		outResult.pHZB = InitHZB(graph, dimensions);
		graph.Export(outResult.pHZB, rasterContext.pPreviousHZB);
	}

	// Debug mode outputs an extra debug buffer containing information for debug statistics/visualization
	if (rasterContext.EnableDebug)
		outResult.pDebugData = graph.Create("GPURender.DebugData", TextureDesc::Create2D(dimensions.x, dimensions.y, ResourceFormat::R32_UINT));

	// Clear all counters
	RGPass& clearPass = graph.AddPass("Clear UAVs", RGPassFlag::Compute)
		.Write({ rasterContext.pCandidateMeshletsCounter, rasterContext.pOccludedInstancesCounter, rasterContext.pVisibleMeshletsCounter })
		.Bind([=](CommandContext& context)
			{
				if (outResult.pDebugData)
					context.ClearUAVu(outResult.pDebugData->Get()->GetUAV());
				context.ClearUAVu(rasterContext.pCandidateMeshletsCounter->Get()->GetUAV());
				context.ClearUAVu(rasterContext.pOccludedInstancesCounter->Get()->GetUAV());
				context.ClearUAVu(rasterContext.pVisibleMeshletsCounter->Get()->GetUAV());
				context.InsertUAVBarrier();
			});
	if (outResult.pDebugData)
		clearPass.Write(outResult.pDebugData);

	{
		RG_GRAPH_SCOPE("Phase 1", graph);
		CullAndRasterize(graph, pView, pViewTransform, RasterPhase::Phase1, rasterContext, outResult);
	}

	// If occlusion culling is disabled, phase 1 will already have rendered everything and phase 2 in no longer required.
	if (rasterContext.EnableOcclusionCulling)
	{
		RG_GRAPH_SCOPE("Phase 2", graph);
		CullAndRasterize(graph, pView, pViewTransform, RasterPhase::Phase2, rasterContext, outResult);
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

				context.BindRootCBV(1, Renderer::GetViewUniforms(pView));
				context.BindResources(3, {
					rasterContext.pCandidateMeshletsCounter->Get()->GetSRV(),
					rasterContext.pOccludedInstancesCounter->Get()->GetSRV(),
					rasterContext.pVisibleMeshletsCounter->Get()->GetSRV(),
					}, 1);
				context.Dispatch(1);
			});
}

RGTexture* GPUDrivenRenderer::InitHZB(RGGraph& graph, const Vector2u& viewDimensions) const
{
	Vector2u hzbDimensions;
	hzbDimensions.x = Math::Max(Math::NextPowerOfTwo(viewDimensions.x) >> 1u, 1u);
	hzbDimensions.y = Math::Max(Math::NextPowerOfTwo(viewDimensions.y) >> 1u, 1u);
	uint32 numMips = (uint32)Math::Floor(log2f((float)Math::Max(hzbDimensions.x, hzbDimensions.y)));
	TextureDesc desc = TextureDesc::Create2D(hzbDimensions.x, hzbDimensions.y, ResourceFormat::R16_FLOAT, TextureFlag::UnorderedAccess, 1, numMips);
	return graph.Create("HZB", desc);
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
				context.SetComputeRootSignature(m_pCommonRS);
				context.SetPipelineState(m_pHZBInitializePSO);

				struct
				{
					Vector2 DimensionsInv;
				} parameters;

				parameters.DimensionsInv = Vector2(1.0f / hzbDimensions.x, 1.0f / hzbDimensions.y);
				context.BindRootCBV(0, parameters);
				context.BindResources(2, pHZB->Get()->GetUAV());
				context.BindResources(3, pDepth->Get()->GetSRV());
				context.Dispatch(ComputeUtils::GetNumThreadGroups(hzbDimensions.x, 16, hzbDimensions.y, 16));
			});

	RGBuffer* pSPDCounter = graph.Create("SPD.Counter", BufferDesc::CreateTyped(1, ResourceFormat::R32_UINT));

	graph.AddPass("HZB Mips", RGPassFlag::Compute)
		.Write({ pHZB, pSPDCounter })
		.Bind([=](CommandContext& context)
			{
				context.ClearUAVu(pSPDCounter->Get()->GetUAV());
				context.InsertUAVBarrier();

				context.SetComputeRootSignature(m_pCommonRS);
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

				context.BindRootCBV(0, parameters);
				uint32 uavIndex = 0;
				context.BindResources(2, pSPDCounter->Get()->GetUAV(), uavIndex++);
				if(pHZB->GetDesc().Mips > 6)
					context.BindResources(2, pHZB->Get()->GetSubResourceUAV(6), uavIndex++);
				for (uint8 mipIndex = 0; mipIndex < mips; ++mipIndex)
				{
					context.BindResources(2, pHZB->Get()->GetSubResourceUAV(mipIndex), uavIndex++);
				}
				context.Dispatch(dispatchThreadGroupCountXY[0], dispatchThreadGroupCountXY[1]);
			});
}

