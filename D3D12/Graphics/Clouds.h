#pragma once

class ComputePipelineState;
class GraphicsPipelineState;
class RootSignature;
class Texture3D;
class Graphics;
class Texture2D;
class VertexBuffer;

class Clouds
{
public:
	void Initialize(Graphics* pGraphics);
	void RenderUI();
	void Render(Graphics* pGraphics, Texture2D* pSceneTexture, Texture2D* pDepthTexture);

private:
	std::unique_ptr<ComputePipelineState> m_pWorleyNoisePS;
	std::unique_ptr<RootSignature> m_pWorleyNoiseRS;
	std::unique_ptr<Texture3D> m_pWorleyNoiseTexture;

	std::unique_ptr<GraphicsPipelineState> m_pCloudsPS;
	std::unique_ptr<RootSignature> m_pCloudsRS;

	std::unique_ptr<Texture2D> m_pIntermediateColor;
	std::unique_ptr<Texture2D> m_pIntermediateDepth;

	std::unique_ptr<VertexBuffer> m_pQuadVertexBuffer;
};