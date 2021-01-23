#include "stdafx.h"
#include "PipelineState.h"
#include "Shader.h"
#include "Graphics.h"

PipelineState::PipelineState(Graphics* pParent)
	: GraphicsObject(pParent)
{
	m_Desc.GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND>() = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	m_Desc.GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1>() = CD3DX12_DEPTH_STENCIL_DESC1(D3D12_DEFAULT);
	m_Desc.GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER>() = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	m_Desc.GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC>() = DefaultSampleDesc();
	m_Desc.GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY>() = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	m_Desc.GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS>() = D3D12_PIPELINE_STATE_FLAG_NONE;
	m_Desc.GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK>() = DefaultSampleMask();
}

PipelineState::PipelineState(const PipelineState& other)
	: GraphicsObject(other),
	m_Desc(other.m_Desc),
	m_Type(other.m_Type)
{
}

PipelineState::~PipelineState()
{
}

void PipelineState::Finalize(const char* pName)
{
	check(m_Type != PipelineStateType::MAX);
	ComPtr<ID3D12Device2> pDevice2;
	VERIFY_HR_EX(GetParent()->GetDevice()->QueryInterface(IID_PPV_ARGS(pDevice2.GetAddressOf())), GetParent()->GetDevice());
	D3D12_PIPELINE_STATE_STREAM_DESC streamDesc = m_Desc.Desc();
	VERIFY_HR_EX(pDevice2->CreatePipelineState(&streamDesc, IID_PPV_ARGS(m_pPipelineState.GetAddressOf())), GetParent()->GetDevice());
	D3D::SetObjectName(m_pPipelineState.Get(), pName);
}

void PipelineState::SetRenderTargetFormat(DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat, uint32 msaa)
{
	SetRenderTargetFormats(&rtvFormat, 1, dsvFormat, msaa);
}

void PipelineState::SetRenderTargetFormats(DXGI_FORMAT* rtvFormats, uint32 count, DXGI_FORMAT dsvFormat, uint32 msaa)
{
	D3D12_RT_FORMAT_ARRAY& formatArray = m_Desc.GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS>();
	formatArray.NumRenderTargets = count;
	for (uint32 i = 0; i < count; ++i)
	{
		formatArray.RTFormats[i] = rtvFormats[i];
	}
	DXGI_SAMPLE_DESC& sampleDesc = m_Desc.GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC>();
	sampleDesc.Count = msaa;
	sampleDesc.Quality = 0;
	m_Desc.GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER>().MultisampleEnable = msaa > 1;

	m_Desc.GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT>() = dsvFormat;
}

void PipelineState::SetBlendMode(const BlendMode& blendMode, bool /*alphaToCoverage*/)
{
	D3D12_BLEND_DESC& blendDesc = m_Desc.GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND>();
	D3D12_RENDER_TARGET_BLEND_DESC& desc = blendDesc.RenderTarget[0];
	desc.RenderTargetWriteMask = 0xf;
	desc.BlendEnable = blendMode == BlendMode::Replace ? false : true;

	switch (blendMode)
	{
	case BlendMode::Replace:
		desc.SrcBlend = D3D12_BLEND_ONE;
		desc.DestBlend = D3D12_BLEND_ZERO;
		desc.BlendOp = D3D12_BLEND_OP_ADD;
		desc.SrcBlendAlpha = D3D12_BLEND_ONE;
		desc.DestBlendAlpha = D3D12_BLEND_ZERO;
		desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		break;
	case BlendMode::Alpha:
		desc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
		desc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		desc.BlendOp = D3D12_BLEND_OP_ADD;
		desc.SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA;
		desc.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
		desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		break;
	case BlendMode::Additive:
		desc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
		desc.DestBlend = D3D12_BLEND_ONE;
		desc.BlendOp = D3D12_BLEND_OP_ADD;
		desc.SrcBlendAlpha = D3D12_BLEND_ONE;
		desc.DestBlendAlpha = D3D12_BLEND_ONE;
		desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		break;
	case BlendMode::Multiply:
		desc.SrcBlend = D3D12_BLEND_DEST_COLOR;
		desc.DestBlend = D3D12_BLEND_ZERO;
		desc.BlendOp = D3D12_BLEND_OP_ADD;
		desc.SrcBlendAlpha = D3D12_BLEND_DEST_COLOR;
		desc.DestBlendAlpha = D3D12_BLEND_ZERO;
		desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		break;
	case BlendMode::AddAlpha:
		desc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
		desc.DestBlend = D3D12_BLEND_ONE;
		desc.BlendOp = D3D12_BLEND_OP_ADD;
		desc.SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA;
		desc.DestBlendAlpha = D3D12_BLEND_ONE;
		desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		break;
	case BlendMode::PreMultiplyAlpha:
		desc.SrcBlend = D3D12_BLEND_ONE;
		desc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		desc.BlendOp = D3D12_BLEND_OP_ADD;
		desc.SrcBlendAlpha = D3D12_BLEND_ONE;
		desc.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
		desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		break;
	case BlendMode::InverseDestinationAlpha:
		desc.SrcBlend = D3D12_BLEND_INV_DEST_ALPHA;
		desc.DestBlend = D3D12_BLEND_DEST_ALPHA;
		desc.BlendOp = D3D12_BLEND_OP_ADD;
		desc.SrcBlendAlpha = D3D12_BLEND_INV_DEST_ALPHA;
		desc.DestBlendAlpha = D3D12_BLEND_DEST_ALPHA;
		desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		break;
	case BlendMode::Subtract:
		desc.SrcBlend = D3D12_BLEND_ONE;
		desc.DestBlend = D3D12_BLEND_ONE;
		desc.BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
		desc.SrcBlendAlpha = D3D12_BLEND_ONE;
		desc.DestBlendAlpha = D3D12_BLEND_ONE;
		desc.BlendOpAlpha = D3D12_BLEND_OP_REV_SUBTRACT;
		break;
	case BlendMode::SubtractAlpha:
		desc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
		desc.DestBlend = D3D12_BLEND_ONE;
		desc.BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
		desc.SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA;
		desc.DestBlendAlpha = D3D12_BLEND_ONE;
		desc.BlendOpAlpha = D3D12_BLEND_OP_REV_SUBTRACT;
		break;
	case BlendMode::Undefined:
	default:
		break;
	}
}

void PipelineState::SetDepthEnabled(bool enabled)
{
	m_Desc.GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1>().DepthEnable = enabled;
}

void PipelineState::SetDepthWrite(bool enabled)
{
	m_Desc.GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1>().DepthWriteMask = enabled ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
}

void PipelineState::SetDepthTest(const D3D12_COMPARISON_FUNC func)
{
	m_Desc.GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1>().DepthFunc = func;
}

void PipelineState::SetStencilTest(bool stencilEnabled, D3D12_COMPARISON_FUNC mode, D3D12_STENCIL_OP pass, D3D12_STENCIL_OP fail, D3D12_STENCIL_OP zFail, unsigned int /*stencilRef*/, unsigned char compareMask, unsigned char writeMask)
{
	D3D12_DEPTH_STENCIL_DESC1& dssDesc = m_Desc.GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1>();
	dssDesc.StencilEnable = stencilEnabled;
	dssDesc.FrontFace.StencilFunc = mode;
	dssDesc.FrontFace.StencilPassOp = pass;
	dssDesc.FrontFace.StencilFailOp = fail;
	dssDesc.FrontFace.StencilDepthFailOp = zFail;
	dssDesc.StencilReadMask = compareMask;
	dssDesc.StencilWriteMask = writeMask;
	dssDesc.BackFace = dssDesc.FrontFace;
}

void PipelineState::SetFillMode(D3D12_FILL_MODE fillMode)
{
	m_Desc.GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER>().FillMode = fillMode;
}

void PipelineState::SetCullMode(D3D12_CULL_MODE cullMode)
{
	m_Desc.GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER>().CullMode = cullMode;
}

void PipelineState::SetLineAntialias(bool lineAntiAlias)
{
	m_Desc.GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER>().AntialiasedLineEnable = lineAntiAlias;
}

void PipelineState::SetDepthBias(int depthBias, float depthBiasClamp, float slopeScaledDepthBias)
{
	D3D12_RASTERIZER_DESC& rsDesc = m_Desc.GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER>();
	rsDesc.SlopeScaledDepthBias = slopeScaledDepthBias;
	rsDesc.DepthBias = depthBias;
	rsDesc.DepthBiasClamp = depthBiasClamp;
}

void PipelineState::SetInputLayout(D3D12_INPUT_ELEMENT_DESC* pElements, uint32 count)
{
	D3D12_INPUT_LAYOUT_DESC& ilDesc = m_Desc.GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT>();
	ilDesc.NumElements = count;
	ilDesc.pInputElementDescs = pElements;
}

void PipelineState::SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE topology)
{
	m_Desc.GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY>() = topology;
}

void PipelineState::SetRootSignature(ID3D12RootSignature* pRootSignature)
{
	m_Desc.GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE>() = pRootSignature;
}

void PipelineState::SetVertexShader(Shader* pShader)
{
	m_Type = PipelineStateType::Graphics;
	m_Desc.GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS>() = { pShader->GetByteCode(), pShader->GetByteCodeSize() };
}

void PipelineState::SetPixelShader(Shader* pShader)
{
	m_Desc.GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS>() = { pShader->GetByteCode(), pShader->GetByteCodeSize() };
}

void PipelineState::SetHullShader(Shader* pShader)
{
	m_Type = PipelineStateType::Graphics;
	m_Desc.GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS>() = { pShader->GetByteCode(), pShader->GetByteCodeSize() };
}

void PipelineState::SetDomainShader(Shader* pShader)
{
	m_Type = PipelineStateType::Graphics;
	m_Desc.GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS>() = { pShader->GetByteCode(), pShader->GetByteCodeSize() };
}

void PipelineState::SetGeometryShader(Shader* pShader)
{
	m_Type = PipelineStateType::Graphics;
	m_Desc.GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS>() = { pShader->GetByteCode(), pShader->GetByteCodeSize() };
}

void PipelineState::SetComputeShader(Shader* pShader)
{
	m_Type = PipelineStateType::Compute;
	m_Desc.GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS>() = { pShader->GetByteCode(), pShader->GetByteCodeSize() };
}

void PipelineState::SetMeshShader(Shader* pShader)
{
	m_Type = PipelineStateType::Mesh;
	m_Desc.GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS>() = { pShader->GetByteCode(), pShader->GetByteCodeSize() };
}

void PipelineState::SetAmplificationShader(Shader* pShader)
{
	m_Type = PipelineStateType::Mesh;
	m_Desc.GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS>() = { pShader->GetByteCode(), pShader->GetByteCodeSize() };
}
