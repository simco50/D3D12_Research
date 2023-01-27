#pragma once
#include "../RenderGraph/RenderGraphDefinitions.h"

class GraphicsDevice;
class RootSignature;
class PipelineState;
class Texture;
struct SceneView;

enum class RasterType
{
	VisibilityBuffer,
};

struct RasterContext
{
	RasterContext(RGGraph& graph, const std::string contextString, RGTexture* pDepth, RefCountPtr<Texture>* pPreviousHZB, RasterType type);

	std::string ContextString;
	RGTexture* pDepth = nullptr;
	RefCountPtr<Texture>* pPreviousHZB = nullptr;
	RasterType Type;

	RGBuffer* pMeshletCandidates = nullptr;
	RGBuffer* pMeshletCandidatesCounter = nullptr;
	RGBuffer* pOccludedInstances = nullptr;
	RGBuffer* pOccludedInstancesCounter = nullptr;
};

struct RasterResult
{
	RGBuffer* pMeshletCandidates = nullptr;
	RGTexture* pVisibilityBuffer = nullptr;
	RGTexture* pHZB = nullptr;
};

class GPUDrivenRenderer
{
public:
	GPUDrivenRenderer(GraphicsDevice* pDevice);
	void Render(RGGraph& graph, const SceneView* pView, const RasterContext& context, RasterResult& outResult);
	void PrintStats(RGGraph& graph, const SceneView* pView, const RasterContext& rasterContext);

private:
	RGTexture* InitHZB(RGGraph& graph, const Vector2u& viewDimensions, RefCountPtr<Texture>* pExportTarget = nullptr) const;
	void BuildHZB(RGGraph& graph, RGTexture* pDepth, RGTexture* pHZB);

	RefCountPtr<RootSignature> m_pCommonRS;
	RefCountPtr<PipelineState> m_pCullInstancesPSO[2];
	RefCountPtr<PipelineState> m_pBuildDrawArgsPSO[2];
	RefCountPtr<PipelineState> m_pCullAndDrawPSO[2];
	RefCountPtr<PipelineState> m_pBuildCullArgsPSO;
	RefCountPtr<PipelineState> m_pPrintStatsPSO;

	RefCountPtr<RootSignature> m_pHZBRS;
	RefCountPtr<PipelineState> m_pHZBInitializePSO;
	RefCountPtr<PipelineState> m_pHZBCreatePSO;
};
