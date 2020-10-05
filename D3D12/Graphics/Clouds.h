#pragma once

class PipelineState;
class RootSignature;
class Texture;
class Graphics;
class Buffer;
class CommandContext;
class Camera;
struct Light;

class Clouds
{
public:
	Clouds();
	void Initialize(Graphics* pGraphics);
	void Render(CommandContext& context, Texture* pSceneTexture, Texture* pDepthTexture, Camera* pCamera, const Light& sunLight);
	Texture* GetNoiseTexture() const { return m_pWorleyNoiseTexture.get(); }

private:
	std::unique_ptr<PipelineState> m_pWorleyNoisePS;
	std::unique_ptr<RootSignature> m_pWorleyNoiseRS;
	std::unique_ptr<Texture> m_pWorleyNoiseTexture;

	std::unique_ptr<PipelineState> m_pCloudsPS;
	std::unique_ptr<RootSignature> m_pCloudsRS;

	std::unique_ptr<Texture> m_pIntermediateColor;
	std::unique_ptr<Texture> m_pIntermediateDepth;

	std::unique_ptr<Buffer> m_pQuadVertexBuffer;

	bool m_UpdateNoise = true;
	BoundingBox m_CloudBounds;
};