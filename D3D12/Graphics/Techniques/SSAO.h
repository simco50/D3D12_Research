#pragma once
class GraphicsDevice;
class RootSignature;
class Texture;
class Camera;
class RGGraph;
class PipelineState;

class SSAO
{
public:
	SSAO(GraphicsDevice* pDevice);

	void OnResize(int windowWidth, int windowHeight);
	void Execute(RGGraph& graph, Texture* pColor, Texture* pDepth, Camera& camera);

private:
	void SetupPipelines();

	GraphicsDevice* m_pDevice;

	std::unique_ptr<Texture> m_pAmbientOcclusionIntermediate;
	std::unique_ptr<RootSignature> m_pSSAORS;
	PipelineState* m_pSSAOPSO = nullptr;
	std::unique_ptr<RootSignature> m_pSSAOBlurRS;
	PipelineState* m_pSSAOBlurPSO = nullptr;
};

