#pragma once
#include "RHI/RHI.h"
#include "RenderGraph/RenderGraphDefinitions.h"

struct RenderView;

enum class RasterMode
{
	VisibilityBuffer,
	Shadows,
};

struct RasterContext
{
	RasterContext(RGGraph& graph, RGTexture* pDepth, RasterMode mode, Ref<Texture>* pPreviousHZB);

	RGTexture* pDepth = nullptr;
	Ref<Texture>* pPreviousHZB = nullptr;
	bool EnableDebug = false;
	bool EnableOcclusionCulling = false;
	bool WorkGraph = false;
	RasterMode Mode;

	RGBuffer* pCandidateMeshlets = nullptr;
	RGBuffer* pCandidateMeshletsCounter = nullptr;
	RGBuffer* pVisibleMeshlets = nullptr;
	RGBuffer* pVisibleMeshletsCounter = nullptr;
	RGBuffer* pOccludedInstances = nullptr;
	RGBuffer* pOccludedInstancesCounter = nullptr;
	RGBuffer* pBinnedMeshletOffsetAndCounts[2]{};
};

struct RasterResult
{
	RGBuffer* pVisibleMeshlets = nullptr;
	RGTexture* pVisibilityBuffer = nullptr;
	RGTexture* pHZB = nullptr;
	RGTexture* pDebugData = nullptr;
};

class MeshletRasterizer
{
public:
	MeshletRasterizer(GraphicsDevice* pDevice);
	void Render(RGGraph& graph, const RenderView* pView, RasterContext& context, RasterResult& outResult);
	void PrintStats(RGGraph& graph, const Vector2& position, const RenderView* pView, const RasterContext& rasterContext);

private:
	enum class RasterPhase
	{
		Phase1,
		Phase2,
	};

	enum class PipelineBin
	{
		Opaque,
		AlphaMasked,
		Count,
	};
	using PipelineStateBinSet = StaticArray<Ref<PipelineState>, (int)PipelineBin::Count>;

	RGTexture* InitHZB(RGGraph& graph, const Vector2u& viewDimensions) const;
	void BuildHZB(RGGraph& graph, RGTexture* pDepth, RGTexture* pHZB);

	void CullAndRasterize(RGGraph& graph, const RenderView* pView, RasterPhase rasterPhase, RasterContext& context, RasterResult& outResult);

	GraphicsDevice* m_pDevice;

	Ref<PipelineState> m_pCullInstancesPSO[2];
	Ref<PipelineState> m_pCullInstancesNoOcclusionPSO;
	Ref<PipelineState> m_pBuildMeshletCullArgsPSO[2];
	Ref<PipelineState> m_pBuildCullArgsPSO;
	Ref<PipelineState> m_pPrintStatsPSO;

	Ref<PipelineState> m_pCullMeshletsPSO[2];
	Ref<PipelineState> m_pCullMeshletsNoOcclusionPSO;

	PipelineStateBinSet m_pDrawMeshletsPSO;
	PipelineStateBinSet m_pDrawMeshletsDebugModePSO;
	PipelineStateBinSet m_pDrawMeshletsDepthOnlyPSO;

	Ref<PipelineState> m_pMeshletBinPrepareArgs;
	Ref<PipelineState> m_pMeshletClassify;
	Ref<PipelineState> m_pMeshletAllocateBinRanges;
	Ref<PipelineState> m_pMeshletWriteBins;

	Ref<PipelineState> m_pHZBInitializePSO;
	Ref<PipelineState> m_pHZBCreatePSO;

	Ref<PipelineState> m_pClearCountersPSO;

	Buffer* m_pWorkGraphMemory = nullptr;
	Ref<StateObject> m_pWorkGraphSO[2];
	Ref<StateObject> m_pWorkGraphNoOcclusionSO;
};
