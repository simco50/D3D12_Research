#pragma once
#include "DescriptorHandle.h"
class CommandContext;
class Graphics;
class RootSignature;
class GraphicsPipelineState;
class Texture;

class ImGuiRenderer
{
public:
	ImGuiRenderer(Graphics* pGraphics);
	~ImGuiRenderer();

	void NewFrame();
	void Render(CommandContext& context, Texture* pRenderTarget);
	void OnSwapchainCreated(int windowWidth, int windowHeight);

private:
	void CreatePipeline();
	void InitializeImGui();

	Graphics* m_pGraphics;
	std::unique_ptr<GraphicsPipelineState> m_pPipelineState;
	std::unique_ptr<RootSignature> m_pRootSignature;
	std::unique_ptr<Texture> m_pFontTexture;
	std::unique_ptr<Texture> m_pDepthBuffer;
};

