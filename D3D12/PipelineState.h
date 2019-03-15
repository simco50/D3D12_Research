#pragma once

class Shader;

enum class BlendMode
{
	REPLACE = 0,
	ADD,
	MULTIPLY,
	ALPHA,
	ADDALPHA,
	PREMULALPHA,
	INVDESTALPHA,
	SUBTRACT,
	SUBTRACTALPHA,
	UNDEFINED,
};

class PipelineState
{
public:
	ID3D12PipelineState* GetPipelineState() const { return m_pPipelineState.Get(); }

protected:
	ComPtr<ID3D12PipelineState> m_pPipelineState;
};

class GraphicsPipelineState : public PipelineState
{
public:
	GraphicsPipelineState();

	void SetRenderTargetFormat(DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat, uint32 msaa, uint32 msaaQuality);
	void SetRenderTargetFormats(DXGI_FORMAT* rtvFormats, uint32 count, DXGI_FORMAT dsvFormat, uint32 msaa, uint32 msaaQuality);

	//BlendState
	void SetBlendMode(const BlendMode& blendMode, bool alphaToCoverage);

	//DepthStencilState
	void SetDepthEnabled(bool enabled);
	void SetDepthWrite(bool enabled);
	void SetDepthTest(const D3D12_COMPARISON_FUNC func);
	void SetStencilTest(bool stencilEnabled, D3D12_COMPARISON_FUNC mode, D3D12_STENCIL_OP pass, D3D12_STENCIL_OP fail, D3D12_STENCIL_OP zFail, unsigned int stencilRef, unsigned char compareMask, unsigned char writeMask);

	//RasterizerState
	void SetScissorEnabled(bool enabled);
	void SetMultisampleEnabled(bool enabled);
	void SetFillMode(D3D12_FILL_MODE fillMode);
	void SetCullMode(D3D12_CULL_MODE cullMode);
	void SetLineAntialias(bool lineAntiAlias);
	void SetDepthBias(float depthBias, float depthBiasClamp, float slopeScaledDepthBias);

	void SetInputLayout(D3D12_INPUT_ELEMENT_DESC* pElements, uint32 count);
	void SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE topology);

	void SetRootSignature(ID3D12RootSignature* pRootSignature);

	void SetVertexShader(const void* pByteCode, uint32 byteCodeLength);
	void SetPixelShader(const void* pByteCode, uint32 byteCodeLength);

	void Finalize(ID3D12Device* pDevice);

private:
	D3D12_GRAPHICS_PIPELINE_STATE_DESC m_Desc = {};
};

class ComputePipelineState : public PipelineState
{
public:
	ComputePipelineState();

	void Finalize(ID3D12Device* pDevice);

	void SetRootSignature(ID3D12RootSignature* pRootSignature);
	void SetComputeShader(const void* pByteCode, uint32 byteCodeLength);

private:
	D3D12_COMPUTE_PIPELINE_STATE_DESC m_Desc = {};
};