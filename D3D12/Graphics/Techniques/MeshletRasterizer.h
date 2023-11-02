#pragma once
#include "Graphics/RHI/RHI.h"
#include "Graphics/RenderGraph/RenderGraphDefinitions.h"

struct SceneView;
struct ViewTransform;

enum class RasterMode
{
	VisibilityBuffer,
	Shadows,
};

struct RasterContext
{
	RasterContext(RGGraph& graph, RGTexture* pDepth, RasterMode mode, RefCountPtr<Texture>* pPreviousHZB);

	RGTexture* pDepth = nullptr;
	RefCountPtr<Texture>* pPreviousHZB = nullptr;
	bool EnableDebug = false;
	bool EnableOcclusionCulling = false;
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
	void Render(RGGraph& graph, const SceneView* pView, const ViewTransform* pViewTransform, RasterContext& context, RasterResult& outResult);
	void PrintStats(RGGraph& graph, const Vector2& position, const SceneView* pView, const RasterContext& rasterContext);

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
	using PipelineStateBinSet = std::array<RefCountPtr<PipelineState>, (int)PipelineBin::Count>;

	RGTexture* InitHZB(RGGraph& graph, const Vector2u& viewDimensions) const;
	void BuildHZB(RGGraph& graph, RGTexture* pDepth, RGTexture* pHZB);

	void CullAndRasterize(RGGraph& graph, const SceneView* pView, const ViewTransform* pViewTransform, RasterPhase rasterPhase, RasterContext& context, RasterResult& outResult);

	RefCountPtr<RootSignature> m_pCommonRS;
	
	RefCountPtr<PipelineState> m_pCullInstancesPSO[2];
	RefCountPtr<PipelineState> m_pCullInstancesNoOcclusionPSO;
	RefCountPtr<PipelineState> m_pBuildMeshletCullArgsPSO[2];
	RefCountPtr<PipelineState> m_pBuildCullArgsPSO;
	RefCountPtr<PipelineState> m_pPrintStatsPSO;

	RefCountPtr<PipelineState> m_pCullMeshletsPSO[2];
	RefCountPtr<PipelineState> m_pCullMeshletsNoOcclusionPSO;

	PipelineStateBinSet m_pDrawMeshletsPSO;
	PipelineStateBinSet m_pDrawMeshletsDebugModePSO;
	PipelineStateBinSet m_pDrawMeshletsDepthOnlyPSO;

	RefCountPtr<PipelineState> m_pMeshletBinPrepareArgs;
	RefCountPtr<PipelineState> m_pMeshletClassify;
	RefCountPtr<PipelineState> m_pMeshletAllocateBinRanges;
	RefCountPtr<PipelineState> m_pMeshletWriteBins;

	RefCountPtr<PipelineState> m_pHZBInitializePSO;
	RefCountPtr<PipelineState> m_pHZBCreatePSO;
};
