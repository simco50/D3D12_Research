#pragma once
#include "../RenderGraph/RenderGraphDefinitions.h"

class GraphicsDevice;
class RootSignature;
class PipelineState;
class Texture;
struct SceneView;

class VisibilityBuffer
{
public:
	VisibilityBuffer(GraphicsDevice* pDevice);
	void Render(RGGraph& graph, const SceneView* pView, RGTexture* pDepth, RGTexture** pOutVisibilityBuffer, RGTexture** pOutHZB);
	void BuildHZB(RGGraph& graph, RGTexture* pDepth, RGTexture** pOutHZB, RefCountPtr<Texture>* pExportTarget = nullptr);

private:
	RefCountPtr<Texture> m_pHZB;

	RefCountPtr<RootSignature> m_pCommonRS;
	RefCountPtr<PipelineState> m_pCullInstancesPhase1PSO;
	RefCountPtr<PipelineState> m_pBuildDrawArgsPhase1PSO;
	RefCountPtr<PipelineState> m_pCullAndDrawPhase1PSO;
	RefCountPtr<PipelineState> m_pBuildCullArgsPhase2PSO;
	RefCountPtr<PipelineState> m_pCullInstancesPhase2PSO;
	RefCountPtr<PipelineState> m_pCullAndDrawPhase2PSO;
	RefCountPtr<PipelineState> m_pPrintStatsPSO;

	RefCountPtr<PipelineState> m_pHZBInitializePSO;
	RefCountPtr<PipelineState> m_pHZBCreatePSO;
};
