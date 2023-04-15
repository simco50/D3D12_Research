#pragma once
#include "../RenderGraph/RenderGraphDefinitions.h"

class GraphicsDevice;
class RootSignature;
class PipelineState;
class Texture;
struct SceneView;

struct RasterContext
{
	RasterContext(RGGraph& graph, const std::string contextString, RGTexture* pDepth, RefCountPtr<Texture>* pPreviousHZB);

	std::string ContextString;
	RGTexture* pDepth = nullptr;
	RefCountPtr<Texture>* pPreviousHZB = nullptr;
	bool EnableDebug = false;

	RGBuffer* pCandidateMeshlets = nullptr;
	RGBuffer* pCandidateMeshletsCounter = nullptr;
	RGBuffer* pVisibleMeshlets = nullptr;
	RGBuffer* pVisibleMeshletsCounter = nullptr;
	RGBuffer* pOccludedInstances = nullptr;
	RGBuffer* pOccludedInstancesCounter = nullptr;
};

struct RasterResult
{
	RGBuffer* pVisibleMeshlets = nullptr;
	RGTexture* pVisibilityBuffer = nullptr;
	RGTexture* pHZB = nullptr;
	RGTexture* pDebugData = nullptr;
};

class GPUDrivenRenderer
{
public:
	GPUDrivenRenderer(GraphicsDevice* pDevice);
	void Render(RGGraph& graph, const SceneView* pView, const RasterContext& context, RasterResult& outResult);
	void PrintStats(RGGraph& graph, const SceneView* pView, const RasterContext& rasterContext);

private:
	enum class RasterPhase
	{
		Phase1,
		Phase2,
	};

	RGTexture* InitHZB(RGGraph& graph, const Vector2u& viewDimensions, RefCountPtr<Texture>* pExportTarget = nullptr) const;
	void BuildHZB(RGGraph& graph, RGTexture* pDepth, RGTexture* pHZB);

	void CullAndRasterize(RGGraph& graph, const SceneView* pView, RasterPhase rasterPhase, const RasterContext& context, RasterResult& outResult);

	RefCountPtr<RootSignature> m_pCommonRS;
	
	RefCountPtr<PipelineState> m_pCullInstancesPSO[2];
	RefCountPtr<PipelineState> m_pBuildMeshletCullArgsPSO[2];
	RefCountPtr<PipelineState> m_pBuildCullArgsPSO;
	RefCountPtr<PipelineState> m_pPrintStatsPSO;

	RefCountPtr<PipelineState> m_pCullMeshletsPSO[2];
	RefCountPtr<PipelineState> m_pDrawMeshletsPSO[2];
	RefCountPtr<PipelineState> m_pDrawMeshletsDebugModePSO[2];

	RefCountPtr<PipelineState> m_pMeshletBinPrepareArgs;
	RefCountPtr<PipelineState> m_pMeshletClassify;
	RefCountPtr<PipelineState> m_pMeshletAllocateBinRanges;
	RefCountPtr<PipelineState> m_pMeshletWriteBins;

	RefCountPtr<PipelineState> m_pHZBInitializePSO;
	RefCountPtr<PipelineState> m_pHZBCreatePSO;
};
