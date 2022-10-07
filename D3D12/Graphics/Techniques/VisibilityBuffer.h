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
	RGTexture* Render(RGGraph& graph, const SceneView* pView, RGTexture* pDepth);
	void BuildHZB(RGGraph& graph, RGTexture* pDepth, RGTexture** pOutHZB);

private:
	RefCountPtr<Texture> m_pHZB;

	RefCountPtr<RootSignature> m_pCommonRS;
	RefCountPtr<PipelineState> m_pCullInstancesPhase1PSO;
	RefCountPtr<PipelineState> m_pBuildDrawArgsPhase1PSO;
	RefCountPtr<PipelineState> m_pCullAndDrawPhase1PSO;
	RefCountPtr<PipelineState> m_pBuildCullArgsPhase2PSO;
	RefCountPtr<PipelineState> m_pCullInstancesPhase2PSO;
	RefCountPtr<PipelineState> m_pCullAndDrawPhase2PSO;

	RefCountPtr<PipelineState> m_pHZBInitializePSO;
	RefCountPtr<PipelineState> m_pHZBCreatePSO;
};
