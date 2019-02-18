#pragma once
class CommandContext;
class Graphics;

class ImGuiRenderer
{
public:
	ImGuiRenderer(Graphics* pGraphics);
	~ImGuiRenderer();

	void NewFrame();
	void Render(CommandContext& context);

private:
	void LoadShaders(const char* pFilePath, ComPtr<ID3DBlob>* pVertexShaderCode, ComPtr<ID3DBlob>* pPixelShaderCode);
	void CreateRootSignature();
	void CreatePipelineState(const ComPtr<ID3DBlob>& pVertexShaderCode, const ComPtr<ID3DBlob>& pPixelShaderCode);
	void InitializeImGui();

	Graphics* m_pGraphics;
	ComPtr<ID3D12PipelineState> m_pPipelineState;
	ComPtr<ID3D12RootSignature> m_pRootSignature;
};

