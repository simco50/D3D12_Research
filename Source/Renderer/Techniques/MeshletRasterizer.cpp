#include "stdafx.h"
#include "MeshletRasterizer.h"
#include "Core/ConsoleVariables.h"
#include "Core/Profiler.h"
#include "RHI/Device.h"
#include "RHI/PipelineState.h"
#include "RHI/StateObject.h"
#include "RHI/RootSignature.h"
#include "Renderer/Mesh.h"
#include "Renderer/Renderer.h"
#include "RenderGraph/RenderGraph.h"

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

	constexpr uint32 CullInstanceThreadGroupSize = 64;
	constexpr uint32 CullMeshletThreadGroupSize = 64;
}

MeshletRasterizer::MeshletRasterizer(GraphicsDevice* pDevice)
	: m_pDevice(pDevice)
{
	if (!pDevice->GetCapabilities().SupportsMeshShading())
		return;

	ShaderDefineHelper defines;
	defines.Set("MAX_NUM_MESHLETS", Tweakables::MaxNumMeshlets);
	defines.Set("MAX_NUM_INSTANCES", Tweakables::MaxNumInstances);
	defines.Set("NUM_CULL_INSTANCES_THREADS", Tweakables::CullInstanceThreadGroupSize);
	defines.Set("NUM_CULL_MESHLETS_THREADS", Tweakables::CullMeshletThreadGroupSize);
	defines.Set("NUM_RASTER_BINS", (int)PipelineBin::Count);

	m_pClearCountersPSO = pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "MeshletCull.hlsl", "ClearCountersCS", *defines);

	m_pBuildCullArgsPSO = pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "MeshletCull.hlsl", "BuildInstanceCullIndirectArgs", *defines);

	// Raster PSOs for visibility buffer
	{
		ShaderDefineHelper rasterDefines(defines);

		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(GraphicsCommon::pCommonRS);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		psoDesc.SetRenderTargetFormats(ResourceFormat::R32_UINT, Renderer::DepthStencilFormat, 1);
		psoDesc.SetStencilTest(true, D3D12_COMPARISON_FUNC_ALWAYS, D3D12_STENCIL_OP_REPLACE, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, 0x0, (uint8)StencilBit::SurfaceTypeMask);
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
		psoDesc.SetRootSignature(GraphicsCommon::pCommonRS);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		psoDesc.SetDepthOnlyTarget(Renderer::DepthStencilFormat, 1);
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
	m_pBuildMeshletCullArgsPSO[0] = pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "MeshletCull.hlsl", "BuildMeshletCullIndirectArgs", *defines);
	m_pCullInstancesPSO[0] =		pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "MeshletCull.hlsl", "CullInstancesCS", *defines);
	m_pCullMeshletsPSO[0] =			pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "MeshletCull.hlsl", "CullMeshletsCS", *defines);

	// Second Phase culling PSOs
	defines.Set("OCCLUSION_FIRST_PASS", false);
	m_pBuildMeshletCullArgsPSO[1] = pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "MeshletCull.hlsl", "BuildMeshletCullIndirectArgs", *defines);
	m_pCullInstancesPSO[1] =		pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "MeshletCull.hlsl", "CullInstancesCS", *defines);
	m_pCullMeshletsPSO[1] =			pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "MeshletCull.hlsl", "CullMeshletsCS", *defines);

	// No-occlusion culling PSOs
	defines.Set("OCCLUSION_CULL", false);
	defines.Set("OCCLUSION_FIRST_PASS", true);
	m_pCullInstancesNoOcclusionPSO = pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "MeshletCull.hlsl", "CullInstancesCS", *defines);
	m_pCullMeshletsNoOcclusionPSO = pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "MeshletCull.hlsl", "CullMeshletsCS", *defines);

	// Classification PSOs
	m_pMeshletBinPrepareArgs =		pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "MeshletBinning.hlsl", "PrepareArgsCS", *defines);
	m_pMeshletAllocateBinRanges =	pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "MeshletBinning.hlsl", "AllocateBinRangesCS");
	m_pMeshletClassify =			pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "MeshletBinning.hlsl", "ClassifyMeshletsCS", *defines);
	m_pMeshletWriteBins =			pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "MeshletBinning.hlsl", "WriteBinsCS", *defines);

	// HZB PSOs
	m_pHZBInitializePSO =			pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "HZB.hlsl", "HZBInitCS");
	m_pHZBCreatePSO =				pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "HZB.hlsl", "HZBCreateCS");

	// Debug PSOs
	m_pPrintStatsPSO =				pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "MeshletCull.hlsl", "PrintStatsCS", *defines);

	if (m_pDevice->GetCapabilities().SupportsWorkGraphs())
	{
		{
			defines.Set("OCCLUSION_FIRST_PASS", true);
			defines.Set("OCCLUSION_CULL", true);

			StateObjectInitializer so;
			so.Type = D3D12_STATE_OBJECT_TYPE_EXECUTABLE;
			so.pGlobalRootSignature = GraphicsCommon::pCommonRS;
			so.AddLibrary("MeshletCullWG.hlsl", {}, *defines);
			so.Name = "WG";
			m_pWorkGraphSO[0] = pDevice->CreateStateObject(so);
		}
		{
			defines.Set("OCCLUSION_FIRST_PASS", false);
			defines.Set("OCCLUSION_CULL", true);

			StateObjectInitializer so;
			so.Type = D3D12_STATE_OBJECT_TYPE_EXECUTABLE;
			so.pGlobalRootSignature = GraphicsCommon::pCommonRS;
			so.AddLibrary("MeshletCullWG.hlsl", {}, * defines);
			so.Name = "WG";
			m_pWorkGraphSO[1] = pDevice->CreateStateObject(so);
		}
		{
			defines.Set("OCCLUSION_FIRST_PASS", true);
			defines.Set("OCCLUSION_CULL", false);

			StateObjectInitializer so;
			so.Type = D3D12_STATE_OBJECT_TYPE_EXECUTABLE;
			so.pGlobalRootSignature = GraphicsCommon::pCommonRS;
			so.AddLibrary("MeshletCullWG.hlsl", {}, * defines);
			so.Name = "WG";
			m_pWorkGraphNoOcclusionSO = pDevice->CreateStateObject(so);
		}

		m_pClearRasterBins = pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "MeshletCullWG.hlsl", "ClearRasterBins", *defines);
	}
}

RasterContext::RasterContext(RGGraph& graph, RGTexture* pDepth, RasterMode mode, Ref<Texture>* pPreviousHZB)
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
	pOccludedInstancesCounter	= graph.Create("GPURender.OccludedInstances.Counter",	BufferDesc::CreateStructured(1, sizeof(uint32)));

	// 0: Num Total | 1: Num Phase 1 | 2: Num Phase 2
	pCandidateMeshletsCounter	= graph.Create("GPURender.CandidateMeshlets.Counter",	BufferDesc::CreateStructured(3, sizeof(uint32)));
	// 0: Num Phase 1 | 1: Num Phase 2
	pVisibleMeshletsCounter		= graph.Create("GPURender.VisibleMeshlets.Counter",		BufferDesc::CreateStructured(2, sizeof(uint32)));
}

void MeshletRasterizer::CullAndRasterize(RGGraph& graph, const RenderView* pView, RasterPhase rasterPhase, RasterContext& rasterContext, RasterResult& outResult)
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
	StateObject* pCullWorkGraphSO = m_pWorkGraphSO[psoPhaseIndex];
	PipelineStateBinSet* pRasterPSOs = rasterContext.EnableDebug ? &m_pDrawMeshletsDebugModePSO : &m_pDrawMeshletsPSO;

	if (!rasterContext.EnableOcclusionCulling)
	{
		pCullInstancePSO = m_pCullInstancesNoOcclusionPSO;
		pCullMeshletPSO = m_pCullMeshletsNoOcclusionPSO;
		pCullWorkGraphSO = m_pWorkGraphNoOcclusionSO;
	}

	if (rasterContext.Mode == RasterMode::Shadows)
		pRasterPSOs = &m_pDrawMeshletsDepthOnlyPSO;

	constexpr uint32 numBins = (int)PipelineBin::Count;
	RGBuffer* pMeshletOffsetAndCounts = graph.Create("GPURender.Classify.MeshletOffsetAndCounts", BufferDesc::CreateStructured(numBins, sizeof(Vector4u), BufferFlag::IndirectArguments));
	constexpr uint32 maxNumMeshlets = Tweakables::MaxNumMeshlets;
	RGBuffer* pBinnedMeshlets = graph.Create("GPURender.Classify.BinnedMeshlets", BufferDesc::CreateStructured(maxNumMeshlets, sizeof(uint32)));

	// Store bin data for debugging
	rasterContext.pBinnedMeshletOffsetAndCounts[psoPhaseIndex] = pMeshletOffsetAndCounts;

	if (rasterContext.WorkGraph && m_pDevice->GetCapabilities().SupportsWorkGraphs())
	{
		pCullWorkGraphSO->ConditionallyReload();

		graph.AddPass("Clear Raster Bins", RGPassFlag::Compute)
			.Write({ pMeshletOffsetAndCounts })
			.Bind([=](CommandContext& context, const RGResources& resources)
				{
					context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
					context.SetPipelineState(m_pClearRasterBins);

					context.BindResources(BindingSlot::UAV, resources.GetUAV(pMeshletOffsetAndCounts), 6);
					context.Dispatch(1);

					context.InsertUAVBarrier();
				});

		RGBuffer* pWorkGraphBuffer = graph.Create("Work Graph Buffer", BufferDesc{ .Size = pCullWorkGraphSO->GetWorkgraphBufferSize() });

		RGPass& wgPass = graph.AddPass("Work Graph", RGPassFlag::Compute)
			.Write({ pWorkGraphBuffer })
			.Write({ pBinnedMeshlets, pMeshletOffsetAndCounts })
			.Write({ rasterContext.pCandidateMeshlets, rasterContext.pCandidateMeshletsCounter })
			.Write({ rasterContext.pOccludedInstances, rasterContext.pOccludedInstancesCounter })
			.Write({ rasterContext.pVisibleMeshlets, rasterContext.pVisibleMeshletsCounter })
			.Bind([=](CommandContext& context, const RGResources& resources)
				{
					context.SetComputeRootSignature(GraphicsCommon::pCommonRS);

					D3D12_SET_PROGRAM_DESC programDesc{
						.Type = D3D12_PROGRAM_TYPE_WORK_GRAPH,
						.WorkGraph {
							.ProgramIdentifier = pCullWorkGraphSO->GetStateObjectProperties()->GetProgramIdentifier(L"WG"),
							.Flags = resources.Get(pWorkGraphBuffer) != m_pWorkGraphBuffer[psoPhaseIndex] ? D3D12_SET_WORK_GRAPH_FLAG_INITIALIZE : D3D12_SET_WORK_GRAPH_FLAG_NONE,
							.BackingMemory = { resources.Get(pWorkGraphBuffer)->GetGpuHandle(), resources.Get(pWorkGraphBuffer)->GetSize() },
							.NodeLocalRootArgumentsTable = {},
						}
					};
					context.SetProgram(programDesc);

					m_pWorkGraphBuffer[psoPhaseIndex] = resources.Get(pWorkGraphBuffer);

					struct
					{
						Vector2u HZBDimensions;
					} params;
					params.HZBDimensions = pSourceHZB ? pSourceHZB->GetDesc().Size2D() : Vector2u(0, 0);

					Renderer::BindViewUniforms(context, *pView);
					context.BindRootCBV(BindingSlot::PerInstance, params);
					context.BindResources(BindingSlot::UAV, {
						resources.GetUAV(rasterContext.pCandidateMeshlets),
						resources.GetUAV(rasterContext.pCandidateMeshletsCounter),
						resources.GetUAV(rasterContext.pOccludedInstances),
						resources.GetUAV(rasterContext.pOccludedInstancesCounter),
						resources.GetUAV(rasterContext.pVisibleMeshlets),
						resources.GetUAV(rasterContext.pVisibleMeshletsCounter),
						resources.GetUAV(pMeshletOffsetAndCounts),
						resources.GetUAV(pBinnedMeshlets),
						});

					if (rasterContext.EnableOcclusionCulling)
						context.BindResources(BindingSlot::SRV, resources.GetSRV(pSourceHZB));

					Ref<ID3D12WorkGraphProperties> pProps;
					pCullWorkGraphSO->GetStateObject()->QueryInterface(pProps.GetAddressOf());

					const char* pEntryPoint = rasterPhase == RasterPhase::Phase1 ? "CullInstancesCS" : "KickPhase2NodesCS";
					uint32 gridSize = rasterPhase == RasterPhase::Phase1 ? Math::DivideAndRoundUp((uint32)pView->pRenderer->GetBatches().GetSize(), Tweakables::CullInstanceThreadGroupSize) : 1;

					D3D12_DISPATCH_GRAPH_DESC graphDesc{
						.Mode = D3D12_DISPATCH_MODE_NODE_CPU_INPUT,
						.NodeCPUInput {
							.EntrypointIndex = pProps->GetEntrypointIndex(0, { MULTIBYTE_TO_UNICODE(pEntryPoint), 0 }),
							.NumRecords = 1,
							.pRecords = &gridSize,
							.RecordStrideInBytes = sizeof(uint32),
						},
					};

					context.DispatchGraph(graphDesc);
					context.InsertUAVBarrier();
				});

		if (rasterContext.EnableOcclusionCulling)
			wgPass.Read(pSourceHZB);
	}
	else
	{
		RG_GRAPH_SCOPE("Instance/Meshlet Culling", graph);

		// In Phase 2, build the indirect arguments based on the instance culling results of Phase 1.
		// These are the list of instances which within the frustum, but were considered occluded by Phase 1.
		if (rasterPhase == RasterPhase::Phase2)
		{
			pInstanceCullArgs = graph.Create("GPURender.InstanceCullArgs", BufferDesc::CreateIndirectArguments<D3D12_DISPATCH_ARGUMENTS>(1));
			graph.AddPass("Build Instance Cull Arguments", RGPassFlag::Compute)
				.Read({ rasterContext.pOccludedInstancesCounter })
				.Write({ pInstanceCullArgs })
				.Bind([=](CommandContext& context, const RGResources& resources)
					{
						context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
						context.SetPipelineState(m_pBuildCullArgsPSO);

						context.BindResources(BindingSlot::UAV, resources.GetUAV(pInstanceCullArgs));
						context.BindResources(BindingSlot::SRV, resources.GetSRV(rasterContext.pOccludedInstancesCounter), 1);
						context.Dispatch(1);
					});
		}

		// Process instances and output meshlets of each visible instance.
		// In Phase 1, also output instances which are occluded according to the previous frame's HZB, and have to be retested in Phase 2.
		// In Phase 2, outputs visible meshlets which were considered occluded before, but are not based on the updated HZB created in Phase 1.
		RGPass& cullInstancePass = graph.AddPass("Cull Instances", RGPassFlag::Compute)
			.Write({ rasterContext.pCandidateMeshlets, rasterContext.pCandidateMeshletsCounter, rasterContext.pOccludedInstances, rasterContext.pOccludedInstancesCounter })
			.Bind([=](CommandContext& context, const RGResources& resources)
				{
					context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
					context.SetPipelineState(pCullInstancePSO);

					struct
					{
						Vector2u HZBDimensions;
					} params;
					params.HZBDimensions = pSourceHZB ? pSourceHZB->GetDesc().Size2D() : Vector2u(0, 0);

					context.BindRootCBV(BindingSlot::PerInstance, params);
					Renderer::BindViewUniforms(context, *pView, RenderView::Type::Cull);
					context.BindResources(BindingSlot::UAV, {
						resources.GetUAV(rasterContext.pCandidateMeshlets),
						resources.GetUAV(rasterContext.pCandidateMeshletsCounter),
						resources.GetUAV(rasterContext.pOccludedInstances),
						resources.GetUAV(rasterContext.pOccludedInstancesCounter),
						});

					if (rasterContext.EnableOcclusionCulling)
						context.BindResources(BindingSlot::SRV, resources.GetSRV(pSourceHZB), 2);

					if (rasterPhase == RasterPhase::Phase1)
						context.Dispatch(ComputeUtils::GetNumThreadGroups((uint32)pView->pRenderer->GetBatches().GetSize(), Tweakables::CullInstanceThreadGroupSize));
					else
						context.ExecuteIndirect(GraphicsCommon::pIndirectDispatchSignature, 1, resources.Get(pInstanceCullArgs));
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
			.Bind([=](CommandContext& context, const RGResources& resources)
				{
					context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
					context.SetPipelineState(m_pBuildMeshletCullArgsPSO[psoPhaseIndex]);

					context.BindResources(BindingSlot::UAV, resources.GetUAV(pMeshletCullArgs));
					context.BindResources(BindingSlot::SRV, resources.GetSRV(rasterContext.pCandidateMeshletsCounter));
					context.Dispatch(1);
				});

		// Process the list of meshlets and output a list of visible meshlets.
		// In Phase 1, also output meshlets which were occluded according to the previous frame's HZB, and have to be retested in Phase 2.
		// In Phase 2, outputs visible meshlets which were considered occluded before, but are not based on the updated HZB created in Phase 1.
		RGPass& meshletCullPass = graph.AddPass("Cull Meshlets", RGPassFlag::Compute)
			.Read({ pMeshletCullArgs })
			.Write({ rasterContext.pCandidateMeshlets, rasterContext.pCandidateMeshletsCounter, rasterContext.pVisibleMeshlets, rasterContext.pVisibleMeshletsCounter })
			.Bind([=](CommandContext& context, const RGResources& resources)
				{
					context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
					context.SetPipelineState(pCullMeshletPSO);

					struct
					{
						Vector2u HZBDimensions;
					} params;
					params.HZBDimensions = pSourceHZB ? pSourceHZB->GetDesc().Size2D() : Vector2u(0, 0);

					context.BindRootCBV(BindingSlot::PerInstance, params);
					Renderer::BindViewUniforms(context, *pView, RenderView::Type::Cull);
					context.BindResources(BindingSlot::UAV, {
						resources.GetUAV(rasterContext.pCandidateMeshlets),
						resources.GetUAV(rasterContext.pCandidateMeshletsCounter),
						});
					context.BindResources(BindingSlot::UAV, {
						resources.GetUAV(rasterContext.pVisibleMeshlets),
						resources.GetUAV(rasterContext.pVisibleMeshletsCounter),
						}, 4);

					if (rasterContext.EnableOcclusionCulling)
						context.BindResources(BindingSlot::SRV, resources.GetSRV(pSourceHZB), 2);

					context.ExecuteIndirect(GraphicsCommon::pIndirectDispatchSignature, 1, resources.Get(pMeshletCullArgs));
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

		{
			RG_GRAPH_SCOPE("Classify Shader Types", graph);

			RGBuffer* pMeshletCounts = graph.Create("GPURender.Classify.MeshletCounts", BufferDesc::CreateStructured(numBins, sizeof(uint32)));
			RGBuffer* pGlobalCount = graph.Create("GPURender.Classify.GlobalCount", BufferDesc::CreateStructured(1, sizeof(uint32)));
			RGBuffer* pClassifyArgs = graph.Create("GPURender.Classify.Args", BufferDesc::CreateIndirectArguments<D3D12_DISPATCH_ARGUMENTS>(1));

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
				.Bind([=](CommandContext& context, const RGResources& resources)
					{
						context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
						context.SetPipelineState(m_pMeshletBinPrepareArgs);

						context.BindRootCBV(BindingSlot::PerInstance, classifyParams);
						context.BindResources(BindingSlot::UAV, {
							resources.GetUAV(pMeshletCounts),
							resources.GetUAV(pGlobalCount),
							resources.GetUAV(pClassifyArgs),
							});
						context.BindResources(BindingSlot::SRV, {
							resources.GetSRV(rasterContext.pVisibleMeshletsCounter),
							}, 1);
						context.Dispatch(1);
						context.InsertUAVBarrier();
					});

			// For each meshlet, find in which bin it belongs and store how many meshlets are in each bin.
			graph.AddPass("Count Meshlets", RGPassFlag::Compute)
				.Read(pClassifyArgs)
				.Read({ rasterContext.pVisibleMeshletsCounter, rasterContext.pVisibleMeshlets })
				.Write(pMeshletCounts)
				.Bind([=](CommandContext& context, const RGResources& resources)
					{
						context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
						context.SetPipelineState(m_pMeshletClassify);

						Renderer::BindViewUniforms(context, *pView);
						context.BindRootCBV(BindingSlot::PerInstance, classifyParams);
						context.BindResources(BindingSlot::UAV, resources.GetUAV(pMeshletCounts));
						context.BindResources(BindingSlot::SRV, {
							resources.GetSRV(rasterContext.pVisibleMeshlets),
							resources.GetSRV(rasterContext.pVisibleMeshletsCounter),
							});
						context.ExecuteIndirect(GraphicsCommon::pIndirectDispatchSignature, 1, resources.Get(pClassifyArgs));
							});

			// Perform a prefix sum on the bin counts to retrieve the first index of each bin.
			graph.AddPass("Compute Bin Offsets", RGPassFlag::Compute)
				.Read({ pMeshletCounts })
				.Write({ pGlobalCount, pMeshletOffsetAndCounts })
				.Bind([=](CommandContext& context, const RGResources& resources)
					{
						context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
						context.SetPipelineState(m_pMeshletAllocateBinRanges);

						context.BindRootCBV(BindingSlot::PerInstance, classifyParams);
						context.BindResources(BindingSlot::UAV, {
							resources.GetUAV(pMeshletOffsetAndCounts),
							resources.GetUAV(pGlobalCount),
							});
						context.BindResources(BindingSlot::SRV, resources.GetSRV(pMeshletCounts));
						context.Dispatch(ComputeUtils::GetNumThreadGroups(numBins, 64));
						context.InsertUAVBarrier();
					});

			// Write the meshlet index of each meshlet into the appropriate bin.
			// This will serve as an indirection list to retrieve meshlets.
			graph.AddPass("Write Bins", RGPassFlag::Compute)
				.Read(pClassifyArgs)
				.Read({ rasterContext.pVisibleMeshletsCounter, rasterContext.pVisibleMeshlets })
				.Write({ pMeshletOffsetAndCounts, pBinnedMeshlets })
				.Bind([=](CommandContext& context, const RGResources& resources)
					{
						context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
						context.SetPipelineState(m_pMeshletWriteBins);

						Renderer::BindViewUniforms(context, *pView);
						context.BindRootCBV(BindingSlot::PerInstance, classifyParams);
						context.BindResources(BindingSlot::UAV, {
							resources.GetUAV(pMeshletOffsetAndCounts),
							resources.GetUAV(pBinnedMeshlets),
							});
						context.BindResources(BindingSlot::SRV, {
							resources.GetSRV(rasterContext.pVisibleMeshlets),
							resources.GetSRV(rasterContext.pVisibleMeshletsCounter),
							});
						context.ExecuteIndirect(GraphicsCommon::pIndirectDispatchSignature, 1, resources.Get(pClassifyArgs));
							});
		}
	}

	// Finally, using the list of visible meshlets and classification data, rasterize the meshlets.
	// For each bin, we bind the associated PSO and record an indirect DispatchMesh.
	const RenderPassDepthFlags depthFlags = rasterPhase == RasterPhase::Phase1 ? RenderPassDepthFlags::Clear : RenderPassDepthFlags::None;
	RGPass& drawPass = graph.AddPass("Rasterize", RGPassFlag::Raster)
		.Read({ rasterContext.pVisibleMeshlets, pMeshletOffsetAndCounts, pBinnedMeshlets })
		.Write(outResult.pDebugData)
		.DepthStencil(rasterContext.pDepth, depthFlags)
		.Bind([=](CommandContext& context, const RGResources& resources)
			{
				context.SetGraphicsRootSignature(GraphicsCommon::pCommonRS);
				context.SetStencilRef((uint32)StencilBit::VisibilityBuffer);

				Renderer::BindViewUniforms(context, *pView);
				if (outResult.pDebugData)
					context.BindResources(BindingSlot::UAV, resources.GetUAV(outResult.pDebugData));
				context.BindResources(BindingSlot::SRV, {
					resources.GetSRV(rasterContext.pVisibleMeshlets),
					resources.GetSRV(pBinnedMeshlets),
					resources.GetSRV(pMeshletOffsetAndCounts),
					});

				static constexpr const char* PipelineBinToString[] = {
					"Opaque",
					"Alpha Masked"
				};
				static_assert(ARRAYSIZE(PipelineBinToString) == (int)PipelineBin::Count);

				for (uint32 binIndex = 0; binIndex < numBins; ++binIndex)
				{
					PROFILE_GPU_SCOPE(context.GetCommandList(), Sprintf("Raster Bin - %s", PipelineBinToString[binIndex]).c_str());

					struct
					{
						uint32 BinIndex;
					} params;
					params.BinIndex = binIndex;
					context.BindRootCBV(BindingSlot::PerInstance, params);
					context.SetPipelineState(pRasterPSOs->at(binIndex));
					context.ExecuteIndirect(GraphicsCommon::pIndirectDispatchMeshSignature, 1, resources.Get(pMeshletOffsetAndCounts), nullptr, sizeof(Vector4u) * binIndex);
				}
			});

	if (outResult.pVisibilityBuffer)
	{
		const RenderPassColorFlags colorFlags = rasterPhase == RasterPhase::Phase1 ? RenderPassColorFlags::Clear : RenderPassColorFlags::None;
		drawPass.RenderTarget(outResult.pVisibilityBuffer, colorFlags);
	}

	// Build the HZB, this HZB must be persistent across frames for this system to work.
	// In Phase 1, the HZB is built so it can be used in Phase 2 for accurrate occlusion culling.
	// In Phase 2, the HZB is built to be used by Phase 1 in the next frame.
	if (rasterContext.EnableOcclusionCulling && !pView->FreezeCull)
		BuildHZB(graph, rasterContext.pDepth, outResult.pHZB);
}

void MeshletRasterizer::Render(RGGraph& graph, const RenderView* pView, RasterContext& rasterContext, RasterResult& outResult)
{
	gAssert(!rasterContext.EnableOcclusionCulling || rasterContext.pPreviousHZB, "Occlusion Culling required previous frame's HZB");

	RG_GRAPH_SCOPE("Cull and Rasterize", graph);

#if _DEBUG
	// Validate that we don't have more meshlets/instances than allowed.
	uint32 numMeshlets = 0;
	for (const Batch& b : pView->pRenderer->GetBatches())
		numMeshlets += b.pMesh->NumMeshlets;
	gAssert(pView->pRenderer->GetBatches().GetSize() <= Tweakables::MaxNumInstances);
	gAssert(numMeshlets <= Tweakables::MaxNumMeshlets);
#endif

	Vector2u dimensions = rasterContext.pDepth->GetDesc().Size2D();
	outResult.pHZB = nullptr;
	outResult.pVisibilityBuffer = nullptr;
	if (rasterContext.Mode == RasterMode::VisibilityBuffer)
		outResult.pVisibilityBuffer = graph.Create("Visibility", TextureDesc::Create2D(dimensions.x, dimensions.y, ResourceFormat::R32_UINT));

	if (rasterContext.EnableOcclusionCulling)
	{
		outResult.pHZB = InitHZB(graph, dimensions);
		graph.Export(outResult.pHZB, rasterContext.pPreviousHZB, TextureFlag::ShaderResource);
	}

	// Debug mode outputs an extra debug buffer containing information for debug statistics/visualization
	if (rasterContext.EnableDebug)
		outResult.pDebugData = graph.Create("GPURender.DebugData", TextureDesc::Create2D(dimensions.x, dimensions.y, ResourceFormat::R32_UINT));

	// Clear all counters
	RGPass& clearPass = graph.AddPass("Clear UAVs", RGPassFlag::Compute)
		.Write({ rasterContext.pCandidateMeshletsCounter, rasterContext.pOccludedInstancesCounter, rasterContext.pVisibleMeshletsCounter })
		.Bind([=](CommandContext& context, const RGResources& resources)
			{
				if (outResult.pDebugData)
					context.ClearTextureUInt(resources.Get(outResult.pDebugData));

				context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
				context.SetPipelineState(m_pClearCountersPSO);

				context.BindResources(BindingSlot::UAV, resources.GetUAV(rasterContext.pCandidateMeshletsCounter), 1);
				context.BindResources(BindingSlot::UAV, resources.GetUAV(rasterContext.pOccludedInstancesCounter), 3);
				context.BindResources(BindingSlot::UAV, resources.GetUAV(rasterContext.pVisibleMeshletsCounter), 5);

				context.Dispatch(1);
				context.InsertUAVBarrier();
			});
	if (outResult.pDebugData)
		clearPass.Write(outResult.pDebugData);

	{
		RG_GRAPH_SCOPE("Phase 1", graph);
		CullAndRasterize(graph, pView, RasterPhase::Phase1, rasterContext, outResult);
	}

	// If occlusion culling is disabled, phase 1 will already have rendered everything and phase 2 in no longer required.
	if (rasterContext.EnableOcclusionCulling)
	{
		RG_GRAPH_SCOPE("Phase 2", graph);
		CullAndRasterize(graph, pView, RasterPhase::Phase2, rasterContext, outResult);
	}

	outResult.pVisibleMeshlets = rasterContext.pVisibleMeshlets;
}

void MeshletRasterizer::PrintStats(RGGraph& graph, const Vector2& position, const RenderView* pView, const RasterContext& rasterContext)
{
	RGBuffer* pDummy = graph.Create("Dummy", BufferDesc::CreateTyped(10, ResourceFormat::RGBA8_UINT));
	RGBuffer* pBins0 = rasterContext.pBinnedMeshletOffsetAndCounts[0] ? rasterContext.pBinnedMeshletOffsetAndCounts[0] : pDummy;
	RGBuffer* pBins1 = rasterContext.pBinnedMeshletOffsetAndCounts[1] ? rasterContext.pBinnedMeshletOffsetAndCounts[1] : pDummy;

	graph.AddPass("Print Stats", RGPassFlag::Compute | RGPassFlag::NeverCull)
		.Read({ rasterContext.pOccludedInstancesCounter, rasterContext.pCandidateMeshletsCounter, rasterContext.pVisibleMeshletsCounter, pBins0, pBins1 })
		.Bind([=](CommandContext& context, const RGResources& resources)
			{
				context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
				context.SetPipelineState(m_pPrintStatsPSO);

				struct
				{
					Vector2 Position;
					uint32 NumBins;
				} params;
				params.Position = position;
				params.NumBins = pBins0->GetDesc().NumElements();

				Renderer::BindViewUniforms(context, *pView);
				context.BindRootCBV(BindingSlot::PerInstance, params);
				context.BindResources(BindingSlot::SRV, {
					resources.GetSRV(rasterContext.pCandidateMeshletsCounter),
					resources.GetSRV(rasterContext.pOccludedInstancesCounter),
					resources.GetSRV(rasterContext.pVisibleMeshletsCounter),
					resources.GetSRV(pBins0),
					resources.GetSRV(pBins1),
					});
				context.Dispatch(1);
			});
}

RGTexture* MeshletRasterizer::InitHZB(RGGraph& graph, const Vector2u& viewDimensions) const
{
	Vector2u hzbDimensions;
	hzbDimensions.x = Math::Max(Math::NextPowerOfTwo(viewDimensions.x) >> 1u, 1u);
	hzbDimensions.y = Math::Max(Math::NextPowerOfTwo(viewDimensions.y) >> 1u, 1u);
	uint32 numMips = (uint32)Math::Floor(log2f((float)Math::Max(hzbDimensions.x, hzbDimensions.y)));
	TextureDesc desc = TextureDesc::Create2D(hzbDimensions.x, hzbDimensions.y, ResourceFormat::R16_FLOAT, numMips);
	return graph.Create("HZB", desc);
}

void MeshletRasterizer::BuildHZB(RGGraph& graph, RGTexture* pDepth, RGTexture* pHZB)
{
	RG_GRAPH_SCOPE("HZB", graph);

	const Vector2u hzbDimensions = pHZB->GetDesc().Size2D();

	graph.AddPass("HZB Create", RGPassFlag::Compute)
		.Read(pDepth)
		.Write(pHZB)
		.Bind([=](CommandContext& context, const RGResources& resources)
			{
				context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
				context.SetPipelineState(m_pHZBInitializePSO);

				struct
				{
					Vector2 DimensionsInv;
				} parameters;

				parameters.DimensionsInv = Vector2(1.0f / hzbDimensions.x, 1.0f / hzbDimensions.y);
				context.BindRootCBV(BindingSlot::PerInstance, parameters);
				context.BindResources(BindingSlot::UAV, resources.GetUAV(pHZB));
				context.BindResources(BindingSlot::SRV, resources.GetSRV(pDepth));
				context.Dispatch(ComputeUtils::GetNumThreadGroups(hzbDimensions.x, 16, hzbDimensions.y, 16));
			});

	RGBuffer* pSPDCounter = graph.Create("SPD.Counter", BufferDesc::CreateTyped(1, ResourceFormat::R32_UINT));

	graph.AddPass("HZB Mips", RGPassFlag::Compute)
		.Write({ pHZB, pSPDCounter })
		.Bind([=](CommandContext& context, const RGResources& resources)
			{
				context.ClearBufferUInt(resources.Get(pSPDCounter));

				context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
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

				context.BindRootCBV(BindingSlot::PerInstance, parameters);
				uint32 uavIndex = 0;
				context.BindResources(BindingSlot::UAV, resources.GetUAV(pSPDCounter), uavIndex++);
				if(pHZB->GetDesc().Mips > 6)
					context.BindResources(BindingSlot::UAV, resources.Get(pHZB)->GetUAV(6), uavIndex++);
				for (uint8 mipIndex = 0; mipIndex < mips; ++mipIndex)
				{
					context.BindResources(BindingSlot::UAV, resources.Get(pHZB)->GetUAV(mipIndex), uavIndex++);
				}
				context.Dispatch(dispatchThreadGroupCountXY[0], dispatchThreadGroupCountXY[1]);
			});
}

