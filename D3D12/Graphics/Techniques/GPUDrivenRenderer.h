#pragma once
#include "../RenderGraph/RenderGraphDefinitions.h"

class GraphicsDevice;
class RootSignature;
class PipelineState;
class Texture;
struct SceneView;

class GPUDrivenRenderer
{
public:
	GPUDrivenRenderer(GraphicsDevice* pDevice);
	void Render(RGGraph& graph, const SceneView* pView, RGTexture* pDepth, RGTexture** pOutVisibilityBuffer, RGTexture** pOutHZB, RefCountPtr<Texture>* pHZBExport);

	RGTexture* InitHZB(RGGraph& graph, const Vector2i& viewDimensions, RefCountPtr<Texture>* pExportTarget = nullptr) const;
	void BuildHZB(RGGraph& graph, RGTexture* pDepth, RGTexture* pHZB);

private:
	RefCountPtr<RootSignature> m_pCommonRS;
	RefCountPtr<PipelineState> m_pCullInstancesPSO[2];
	RefCountPtr<PipelineState> m_pBuildDrawArgsPSO[2];
	RefCountPtr<PipelineState> m_pCullAndDrawPSO[2];
	RefCountPtr<PipelineState> m_pBuildCullArgsPSO;
	RefCountPtr<PipelineState> m_pPrintStatsPSO;

	RefCountPtr<PipelineState> m_pHZBInitializePSO;
	RefCountPtr<PipelineState> m_pHZBCreatePSO;
};
