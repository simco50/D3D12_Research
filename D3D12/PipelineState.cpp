#include "stdafx.h"
#include "PipelineState.h"
#include "Shader.h"

GraphicsPipelineState::GraphicsPipelineState()
{
	m_Desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	m_Desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	m_Desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	m_Desc.SampleDesc.Count = 1;
	m_Desc.SampleDesc.Quality = 0;
	m_Desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	m_Desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	m_Desc.SampleMask = UINT_MAX;
}

GraphicsPipelineState::GraphicsPipelineState(const GraphicsPipelineState& other)
	: m_Desc(other.m_Desc)
{
}

void GraphicsPipelineState::SetRenderTargetFormat(DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat, uint32 msaa, uint32 msaaQuality)
{
	SetRenderTargetFormats(&rtvFormat, 1, dsvFormat, msaa, msaaQuality);
}

void GraphicsPipelineState::SetRenderTargetFormats(DXGI_FORMAT* rtvFormats, uint32 count, DXGI_FORMAT dsvFormat, uint32 msaa, uint32 msaaQuality)
{
	m_Desc.NumRenderTargets = count;
	for (uint32 i = 0; i < count; ++i)
	{
		m_Desc.RTVFormats[i] = rtvFormats[i];
	}
	m_Desc.SampleDesc.Count = msaa;
	m_Desc.SampleDesc.Quality = msaaQuality;
	m_Desc.DSVFormat = dsvFormat;
}

void GraphicsPipelineState::Finalize(ID3D12Device* pDevice)
{
	pDevice->CreateGraphicsPipelineState(&m_Desc, IID_PPV_ARGS(m_pPipelineState.GetAddressOf()));
}

void GraphicsPipelineState::SetBlendMode(const BlendMode& blendMode, bool /*alphaToCoverage*/)
{
	D3D12_RENDER_TARGET_BLEND_DESC& desc = m_Desc.BlendState.RenderTarget[0];
	desc.RenderTargetWriteMask = 0xf;
	desc.BlendEnable = blendMode == BlendMode::REPLACE ? false : true;

	switch (blendMode)
	{
	case BlendMode::REPLACE:
		desc.SrcBlend = D3D12_BLEND_ONE;
		desc.DestBlend = D3D12_BLEND_ZERO;
		desc.BlendOp = D3D12_BLEND_OP_ADD;
		desc.SrcBlendAlpha = D3D12_BLEND_ONE;
		desc.DestBlendAlpha = D3D12_BLEND_ZERO;
		desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		break;
	case BlendMode::ALPHA:
		desc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
		desc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		desc.BlendOp = D3D12_BLEND_OP_ADD;
		desc.SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA;
		desc.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
		desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		break;
	case BlendMode::ADD:
		desc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
		desc.DestBlend = D3D12_BLEND_ONE;
		desc.BlendOp = D3D12_BLEND_OP_ADD;
		desc.SrcBlendAlpha = D3D12_BLEND_ONE;
		desc.DestBlendAlpha = D3D12_BLEND_ONE;
		desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		break;
	case BlendMode::MULTIPLY:
		desc.SrcBlend = D3D12_BLEND_DEST_COLOR;
		desc.DestBlend = D3D12_BLEND_ZERO;
		desc.BlendOp = D3D12_BLEND_OP_ADD;
		desc.SrcBlendAlpha = D3D12_BLEND_DEST_COLOR;
		desc.DestBlendAlpha = D3D12_BLEND_ZERO;
		desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		break;
	case BlendMode::ADDALPHA:
		desc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
		desc.DestBlend = D3D12_BLEND_ONE;
		desc.BlendOp = D3D12_BLEND_OP_ADD;
		desc.SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA;
		desc.DestBlendAlpha = D3D12_BLEND_ONE;
		desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		break;
	case BlendMode::PREMULALPHA:
		desc.SrcBlend = D3D12_BLEND_ONE;
		desc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		desc.BlendOp = D3D12_BLEND_OP_ADD;
		desc.SrcBlendAlpha = D3D12_BLEND_ONE;
		desc.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
		desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		break;
	case BlendMode::INVDESTALPHA:
		desc.SrcBlend = D3D12_BLEND_INV_DEST_ALPHA;
		desc.DestBlend = D3D12_BLEND_DEST_ALPHA;
		desc.BlendOp = D3D12_BLEND_OP_ADD;
		desc.SrcBlendAlpha = D3D12_BLEND_INV_DEST_ALPHA;
		desc.DestBlendAlpha = D3D12_BLEND_DEST_ALPHA;
		desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		break;
	case BlendMode::SUBTRACT:
		desc.SrcBlend = D3D12_BLEND_ONE;
		desc.DestBlend = D3D12_BLEND_ONE;
		desc.BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
		desc.SrcBlendAlpha = D3D12_BLEND_ONE;
		desc.DestBlendAlpha = D3D12_BLEND_ONE;
		desc.BlendOpAlpha = D3D12_BLEND_OP_REV_SUBTRACT;
		break;
	case BlendMode::SUBTRACTALPHA:
		desc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
		desc.DestBlend = D3D12_BLEND_ONE;
		desc.BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
		desc.SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA;
		desc.DestBlendAlpha = D3D12_BLEND_ONE;
		desc.BlendOpAlpha = D3D12_BLEND_OP_REV_SUBTRACT;
		break;
	case BlendMode::UNDEFINED:
	default:
		break;
	}
}

void GraphicsPipelineState::SetDepthEnabled(bool enabled)
{
	m_Desc.DepthStencilState.DepthEnable = enabled;
}

void GraphicsPipelineState::SetDepthWrite(bool enabled)
{
	m_Desc.DepthStencilState.DepthWriteMask = enabled ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
}

void GraphicsPipelineState::SetDepthTest(const D3D12_COMPARISON_FUNC func)
{
	m_Desc.DepthStencilState.DepthFunc = func;
}

void GraphicsPipelineState::SetStencilTest(bool stencilEnabled, D3D12_COMPARISON_FUNC mode, D3D12_STENCIL_OP pass, D3D12_STENCIL_OP fail, D3D12_STENCIL_OP zFail, unsigned int /*stencilRef*/, unsigned char compareMask, unsigned char writeMask)
{
	m_Desc.DepthStencilState.StencilEnable = stencilEnabled;
	m_Desc.DepthStencilState.FrontFace.StencilFunc = mode;
	m_Desc.DepthStencilState.FrontFace.StencilPassOp = pass;
	m_Desc.DepthStencilState.FrontFace.StencilFailOp = fail;
	m_Desc.DepthStencilState.FrontFace.StencilDepthFailOp = zFail;
	m_Desc.DepthStencilState.StencilReadMask = compareMask;
	m_Desc.DepthStencilState.StencilWriteMask = writeMask;
	m_Desc.DepthStencilState.BackFace = m_Desc.DepthStencilState.FrontFace;
}

void GraphicsPipelineState::SetScissorEnabled(bool /*enabled*/)
{

}

void GraphicsPipelineState::SetMultisampleEnabled(bool /*enabled*/)
{

}

void GraphicsPipelineState::SetFillMode(D3D12_FILL_MODE fillMode)
{
	m_Desc.RasterizerState.FillMode = fillMode;
}

void GraphicsPipelineState::SetCullMode(D3D12_CULL_MODE cullMode)
{
	m_Desc.RasterizerState.CullMode = cullMode;
}

void GraphicsPipelineState::SetLineAntialias(bool lineAntiAlias)
{
	m_Desc.RasterizerState.AntialiasedLineEnable = lineAntiAlias;
}

void GraphicsPipelineState::SetDepthBias(int depthBias, float depthBiasClamp, float slopeScaledDepthBias)
{
	m_Desc.RasterizerState.SlopeScaledDepthBias = slopeScaledDepthBias;
	m_Desc.RasterizerState.DepthBias = depthBias;
	m_Desc.RasterizerState.DepthBiasClamp = depthBiasClamp;
}

void GraphicsPipelineState::SetInputLayout(D3D12_INPUT_ELEMENT_DESC* pElements, uint32 count)
{
	m_Desc.InputLayout.NumElements = count;
	m_Desc.InputLayout.pInputElementDescs = pElements;
}

void GraphicsPipelineState::SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE topology)
{
	m_Desc.PrimitiveTopologyType = topology;
}

void GraphicsPipelineState::SetRootSignature(ID3D12RootSignature* pRootSignature)
{
	m_Desc.pRootSignature = pRootSignature;
}

void GraphicsPipelineState::SetVertexShader(const void* pByteCode, uint32 byteCodeLength)
{
	m_Desc.VS.pShaderBytecode = pByteCode;
	m_Desc.VS.BytecodeLength = byteCodeLength;
}

void GraphicsPipelineState::SetPixelShader(const void* pByteCode, uint32 byteCodeLength)
{
	m_Desc.PS.pShaderBytecode = pByteCode;
	m_Desc.PS.BytecodeLength = byteCodeLength;
}

void GraphicsPipelineState::SetGeometryShader(const void* pByteCode, uint32 byteCodeLength)
{
	m_Desc.GS.pShaderBytecode = pByteCode;
	m_Desc.GS.BytecodeLength = byteCodeLength;
}

ComputePipelineState::ComputePipelineState()
{
	m_Desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
}

ComputePipelineState::ComputePipelineState(const ComputePipelineState& other)
	: m_Desc(other.m_Desc)
{

}

void ComputePipelineState::Finalize(ID3D12Device* pDevice)
{
	pDevice->CreateComputePipelineState(&m_Desc, IID_PPV_ARGS(m_pPipelineState.GetAddressOf()));
}

void ComputePipelineState::SetRootSignature(ID3D12RootSignature* pRootSignature)
{
	m_Desc.pRootSignature = pRootSignature;
}

void ComputePipelineState::SetComputeShader(const void* pByteCode, uint32 byteCodeLength)
{
	m_Desc.CS.pShaderBytecode = pByteCode;
	m_Desc.CS.BytecodeLength = byteCodeLength;
}
