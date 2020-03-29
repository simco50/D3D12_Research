#pragma once
#include "RenderGraph/RenderGraph.h"
class Mesh;
class Graphics;
class RootSignature;
class Texture;
class Camera;
class CommandContext;
class Buffer;
class RGGraph;
class PipelineState;

struct SsaoInputResources
{
	Texture* pRenderTarget = nullptr;
	Texture* pNormalsTexture = nullptr;
	Texture* pDepthTexture = nullptr;
	Texture* pNoiseTexture = nullptr;
	Camera* pCamera = nullptr;
};

class SSAO
{
public:
	SSAO(Graphics* pGraphics);

	void OnSwapchainCreated(int windowWidth, int windowHeight);

	void Execute(RGGraph& graph, const SsaoInputResources& resources);

private:
	void SetupResources(Graphics* pGraphics);
	void SetupPipelines(Graphics* pGraphics);

	Graphics* m_pGraphics;

	std::unique_ptr<Texture> m_pAmbientOcclusionIntermediate;
	std::unique_ptr<RootSignature> m_pSSAORS;
	std::unique_ptr<PipelineState> m_pSSAOPSO;
	std::unique_ptr<RootSignature> m_pSSAOBlurRS;
	std::unique_ptr<PipelineState> m_pSSAOBlurPSO;
};

