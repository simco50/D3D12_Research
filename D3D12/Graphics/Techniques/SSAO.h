#pragma once
class GraphicsDevice;
class RootSignature;
class Texture;
class RGGraph;
class PipelineState;
struct SceneView;
struct SceneTextures;

class SSAO
{
public:
	SSAO(GraphicsDevice* pDevice);

	void OnResize(int windowWidth, int windowHeight);
	void Execute(RGGraph& graph, const SceneView& view, const SceneTextures& sceneTextures);

private:
	void SetupPipelines();

	GraphicsDevice* m_pDevice;

	RefCountPtr<Texture> m_pAmbientOcclusionIntermediate;
	RefCountPtr<RootSignature> m_pSSAORS;
	RefCountPtr<PipelineState> m_pSSAOPSO;
	RefCountPtr<PipelineState> m_pSSAOBlurPSO;
};

