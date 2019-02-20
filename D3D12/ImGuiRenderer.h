#pragma once
class CommandContext;
class Graphics;
class RootSignature;
class PipelineState;

class ImGuiRenderer
{
public:
	ImGuiRenderer(Graphics* pGraphics);
	~ImGuiRenderer();

	void NewFrame();
	void Render(CommandContext& context);

private:
	void CreatePipeline();
	void InitializeImGui();

	Graphics* m_pGraphics;
	std::unique_ptr<PipelineState> m_pPipelineState;
	std::unique_ptr<RootSignature> m_pRootSignature;
};

