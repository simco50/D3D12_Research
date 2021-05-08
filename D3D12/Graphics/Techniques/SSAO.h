#pragma once
class GraphicsDevice;
class RootSignature;
class Texture;
class Camera;
class RGGraph;
class PipelineState;
class ShaderManager;

class SSAO
{
public:
	SSAO(ShaderManager* pShaderManager, GraphicsDevice* pDevice);

	void OnSwapchainCreated(int windowWidth, int windowHeight);

	void Execute(RGGraph& graph, Texture* pColor, Texture* pDepth, Camera& camera);

private:
	void SetupResources(GraphicsDevice* pDevice);
	void SetupPipelines(ShaderManager* pShaderManager, GraphicsDevice* pDevice);

	std::unique_ptr<Texture> m_pAmbientOcclusionIntermediate;
	std::unique_ptr<RootSignature> m_pSSAORS;
	PipelineState* m_pSSAOPSO = nullptr;
	std::unique_ptr<RootSignature> m_pSSAOBlurRS;
	PipelineState* m_pSSAOBlurPSO = nullptr;
};

