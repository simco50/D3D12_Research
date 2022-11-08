#pragma once
class GraphicsDevice;
class RootSignature;
class RGGraph;
class PipelineState;
struct SceneView;
struct SceneTextures;

class SSAO
{
public:
	SSAO(GraphicsDevice* pDevice);

	void Execute(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures);

private:
	RefCountPtr<RootSignature> m_pSSAORS;
	RefCountPtr<PipelineState> m_pSSAOPSO;
	RefCountPtr<PipelineState> m_pSSAOBlurPSO;
};
