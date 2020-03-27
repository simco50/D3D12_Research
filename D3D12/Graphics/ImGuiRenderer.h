#pragma once
#include "DescriptorHandle.h"
class CommandContext;
class Graphics;
class RootSignature;
class PipelineState;
class Texture;
class RGGraph;

DECLARE_MULTICAST_DELEGATE(ImGuiCallback);

class ImGuiRenderer
{
public:
	ImGuiRenderer(Graphics* pGraphics);
	~ImGuiRenderer();

	void NewFrame();
	void Render(RGGraph& graph, Texture* pRenderTarget);
	void Update();
	DelegateHandle AddUpdateCallback(ImGuiCallbackDelegate&& callback);
	void RemoveUpdateCallback(DelegateHandle handle);

private:
	void CreatePipeline();
	void InitializeImGui();

	ImGuiCallback m_UpdateCallback;

	Graphics* m_pGraphics;
	std::unique_ptr<PipelineState> m_pPipelineState;
	std::unique_ptr<RootSignature> m_pRootSignature;
	std::unique_ptr<Texture> m_pFontTexture;
};

