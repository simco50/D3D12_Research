#pragma once
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

	void NewFrame(uint32 width, uint32 height);
	void Render(RGGraph& graph, Texture* pRenderTarget);
	void Update();
	DelegateHandle AddUpdateCallback(ImGuiCallbackDelegate&& callback);
	void RemoveUpdateCallback(DelegateHandle handle);

private:
	void CreatePipeline(Graphics* pGraphics);
	void InitializeImGui(Graphics* pGraphics);

	ImGuiCallback m_UpdateCallback;

	std::unique_ptr<PipelineState> m_pPipelineState;
	std::unique_ptr<RootSignature> m_pRootSignature;
	std::unique_ptr<Texture> m_pFontTexture;
};

