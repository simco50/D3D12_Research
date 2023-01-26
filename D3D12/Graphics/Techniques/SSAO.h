#pragma once
#include "../RenderGraph/RenderGraphDefinitions.h"
class GraphicsDevice;
class RootSignature;
class PipelineState;
struct SceneView;
struct SceneTextures;

class SSAO
{
public:
	SSAO(GraphicsDevice* pDevice);

	RGTexture* Execute(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures);

private:
	RefCountPtr<RootSignature> m_pSSAORS;
	RefCountPtr<PipelineState> m_pSSAOPSO;
	RefCountPtr<PipelineState> m_pSSAOBlurPSO;
};
