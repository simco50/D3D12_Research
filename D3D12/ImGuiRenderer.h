#pragma once
class CommandContext;

class ImGuiRenderer
{
public:
	ImGuiRenderer(ID3D12Device* pDevice);
	~ImGuiRenderer();

	void NewFrame();
	void Render(CommandContext& context);

private:
	ID3D12Device* m_pDevice;
	ComPtr<ID3D12PipelineState> m_pPipelineState;
	ComPtr<ID3D12RootSignature> m_pRootSignature;
};

