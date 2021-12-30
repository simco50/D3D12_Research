#pragma once
class GraphicsDevice;
class RootSignature;
class Texture;
class RGGraph;
class PipelineState;
struct SceneView;

class SSAO
{
public:
	SSAO(GraphicsDevice* pDevice);

	void OnResize(int windowWidth, int windowHeight);
	void Execute(RGGraph& graph, Texture* pTarget, const SceneView& sceneData);

private:
	void SetupPipelines();

	GraphicsDevice* m_pDevice;

	std::unique_ptr<Texture> m_pAmbientOcclusionIntermediate;
	std::unique_ptr<RootSignature> m_pSSAORS;
	PipelineState* m_pSSAOPSO = nullptr;
	std::unique_ptr<RootSignature> m_pSSAOBlurRS;
	PipelineState* m_pSSAOBlurPSO = nullptr;
};

