#include "stdafx.h"
#include "PipelineState.h"
#include "Shader.h"
#include "Graphics.h"
#include "RootSignature.h"

PipelineStateInitializer::PipelineStateInitializer()
{
	m_Stream.Blend = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	m_Stream.DepthStencil = CD3DX12_DEPTH_STENCIL_DESC1(D3D12_DEFAULT);
	m_Stream.Rasterizer = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	m_Stream.SampleDesc = DXGI_SAMPLE_DESC{1, 0};
	m_Stream.SampleMask = 0xFFFFFFFF;
	m_Stream.PrimitiveTopology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	m_Stream.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
}

void PipelineStateInitializer::SetName(const char* pName)
{
	m_Name = pName;
}

void PipelineStateInitializer::SetDepthOnlyTarget(ResourceFormat dsvFormat, uint32 msaa)
{
	SetRenderTargetFormats({}, dsvFormat, msaa);
}

void PipelineStateInitializer::SetRenderTargetFormats(const Span<ResourceFormat>& rtvFormats, ResourceFormat dsvFormat, uint32 msaa)
{
	D3D12_RT_FORMAT_ARRAY& formatArray = m_Stream.RTFormats;
	// Validation layer bug - Throws error about RT Format even if NumRenderTargets == 0.
	memset(formatArray.RTFormats, 0, sizeof(DXGI_FORMAT) * ARRAYSIZE(formatArray.RTFormats));
	formatArray.NumRenderTargets = 0;
	for (ResourceFormat format : rtvFormats)
	{
		formatArray.RTFormats[formatArray.NumRenderTargets++] = D3D::ConvertFormat(format);
	}

	DXGI_SAMPLE_DESC& sampleDesc = m_Stream.SampleDesc;
	sampleDesc.Count = msaa;
	sampleDesc.Quality = 0;

	D3D12_RASTERIZER_DESC& rasterDesc = m_Stream.Rasterizer;
	rasterDesc.MultisampleEnable = msaa > 1;

	m_Stream.DSVFormat = D3D::ConvertFormat(dsvFormat);
}

void PipelineStateInitializer::SetBlendMode(const BlendMode& blendMode, bool /*alphaToCoverage*/)
{
	D3D12_BLEND_DESC& blendDesc = m_Stream.Blend;
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

void PipelineStateInitializer::SetDepthEnabled(bool enabled)
{
	D3D12_DEPTH_STENCIL_DESC1& dssDesc = m_Stream.DepthStencil;
	dssDesc.DepthEnable = enabled;
}

void PipelineStateInitializer::SetDepthWrite(bool enabled)
{
	D3D12_DEPTH_STENCIL_DESC1& dssDesc = m_Stream.DepthStencil;
	dssDesc.DepthWriteMask = enabled ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
}

void PipelineStateInitializer::SetDepthTest(D3D12_COMPARISON_FUNC func)
{
	D3D12_DEPTH_STENCIL_DESC1& dssDesc = m_Stream.DepthStencil;
	dssDesc.DepthFunc = func;
}

void PipelineStateInitializer::SetStencilTest(bool stencilEnabled, D3D12_COMPARISON_FUNC mode, D3D12_STENCIL_OP pass, D3D12_STENCIL_OP fail, D3D12_STENCIL_OP zFail, unsigned char compareMask, unsigned char writeMask)
{
	D3D12_DEPTH_STENCIL_DESC1& dssDesc = m_Stream.DepthStencil;
	dssDesc.StencilEnable = stencilEnabled;
	dssDesc.FrontFace.StencilFunc = mode;
	dssDesc.FrontFace.StencilPassOp = pass;
	dssDesc.FrontFace.StencilFailOp = fail;
	dssDesc.FrontFace.StencilDepthFailOp = zFail;
	dssDesc.StencilReadMask = compareMask;
	dssDesc.StencilWriteMask = writeMask;
	dssDesc.BackFace = dssDesc.FrontFace;
}

void PipelineStateInitializer::SetFillMode(D3D12_FILL_MODE fillMode)
{
	D3D12_RASTERIZER_DESC& rasterDesc = m_Stream.Rasterizer;
	rasterDesc.FillMode = fillMode;
}

void PipelineStateInitializer::SetCullMode(D3D12_CULL_MODE cullMode)
{
	D3D12_RASTERIZER_DESC& rasterDesc = m_Stream.Rasterizer;
	rasterDesc.CullMode = cullMode;
}

void PipelineStateInitializer::SetLineAntialias(bool lineAntiAlias)
{
	D3D12_RASTERIZER_DESC& rasterDesc = m_Stream.Rasterizer;
	rasterDesc.AntialiasedLineEnable = lineAntiAlias;
}

void PipelineStateInitializer::SetDepthBias(int depthBias, float depthBiasClamp, float slopeScaledDepthBias)
{
	D3D12_RASTERIZER_DESC& rasterDesc = m_Stream.Rasterizer;
	rasterDesc.SlopeScaledDepthBias = slopeScaledDepthBias;
	rasterDesc.DepthBias = depthBias;
	rasterDesc.DepthBiasClamp = depthBiasClamp;
}

void PipelineStateInitializer::SetInputLayout(const Span<VertexElementDesc>& layout)
{
	D3D12_INPUT_LAYOUT_DESC& ilDesc = m_Stream.InputLayout;

	m_IlDesc.clear();
	for (const VertexElementDesc& element : layout)
	{
		D3D12_INPUT_ELEMENT_DESC& desc = m_IlDesc.emplace_back();
		desc.AlignedByteOffset = element.ByteOffset;
		desc.Format = D3D::ConvertFormat(element.Format);
		desc.InputSlot = 0;
		desc.InputSlotClass = element.InstanceStepRate > 0 ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
		desc.InstanceDataStepRate = element.InstanceStepRate;
		desc.SemanticIndex = 0;
		desc.SemanticName = element.pSemantic;
	}

	ilDesc.NumElements = (uint32)m_IlDesc.size();
	ilDesc.pInputElementDescs = m_IlDesc.data();
}

void PipelineStateInitializer::SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE topology)
{
	m_Stream.PrimitiveTopology = topology;
}

void PipelineStateInitializer::SetRootSignature(RootSignature* pRootSignature)
{
	m_Stream.pRootSignature = pRootSignature->GetRootSignature();
}

void PipelineStateInitializer::SetVertexShader(const char* pShaderPath, const char* entryPoint, const Span<ShaderDefine>& defines)
{
	m_Type = PipelineStateType::Graphics;
	m_ShaderDescs[(int)ShaderType::Vertex] = { pShaderPath, entryPoint, defines.Copy() };
}

void PipelineStateInitializer::SetPixelShader(const char* pShaderPath, const char* entryPoint, const Span<ShaderDefine>& defines)
{
	m_ShaderDescs[(int)ShaderType::Pixel] = { pShaderPath, entryPoint, defines.Copy() };
}

void PipelineStateInitializer::SetComputeShader(const char* pShaderPath, const char* entryPoint, const Span<ShaderDefine>& defines)
{
	m_Type = PipelineStateType::Compute;
	m_ShaderDescs[(int)ShaderType::Compute] = { pShaderPath, entryPoint, defines.Copy() };
}

void PipelineStateInitializer::SetMeshShader(const char* pShaderPath, const char* entryPoint, const Span<ShaderDefine>& defines)
{
	m_Type = PipelineStateType::Mesh;
	m_ShaderDescs[(int)ShaderType::Mesh] = { pShaderPath, entryPoint, defines.Copy() };
}

void PipelineStateInitializer::SetAmplificationShader(const char* pShaderPath, const char* entryPoint, const Span<ShaderDefine>& defines)
{
	m_Type = PipelineStateType::Mesh;
	m_ShaderDescs[(int)ShaderType::Amplification] = { pShaderPath, entryPoint, defines.Copy() };
}

PipelineState::PipelineState(GraphicsDevice* pParent)
	: GraphicsObject(pParent)
{
	m_ReloadHandle = pParent->GetShaderManager()->OnShaderEditedEvent().AddRaw(this, &PipelineState::OnShaderReloaded);
}

PipelineState::~PipelineState()
{
	GetParent()->GetShaderManager()->OnShaderEditedEvent().Remove(m_ReloadHandle);
	GetParent()->DeferReleaseObject(m_pPipelineState.Detach());
}

void PipelineState::Create(const PipelineStateInitializer& initializer)
{
	check(initializer.m_Type != PipelineStateType::MAX);

	m_Desc = initializer;

	if (m_Desc.m_IlDesc.size() > 0)
	{
		D3D12_INPUT_LAYOUT_DESC& ilDesc = m_Desc.m_Stream.InputLayout;
		ilDesc.pInputElementDescs = m_Desc.m_IlDesc.data();
	}

	auto GetByteCode = [this](ShaderType type) -> D3D12_SHADER_BYTECODE& {
		switch (type)
		{
		case ShaderType::Vertex:		return m_Desc.m_Stream.VS;
		case ShaderType::Pixel:			return m_Desc.m_Stream.PS;
		case ShaderType::Mesh:			return m_Desc.m_Stream.MS;
		case ShaderType::Amplification:	return m_Desc.m_Stream.AS;
		case ShaderType::Compute:		return m_Desc.m_Stream.CS;
		case ShaderType::MAX:
		default:
			noEntry();
			static D3D12_SHADER_BYTECODE dummy;
			return dummy;
		}
	};

	bool shaderCompileError = false;
	std::string name = m_Desc.m_Name;
	for (uint32 i = 0; i < (int)ShaderType::MAX; ++i)
	{
		Shader* pShader = nullptr;
		const PipelineStateInitializer::ShaderDesc& desc = m_Desc.m_ShaderDescs[i];
		if (desc.Path.length() > 0)
		{
			pShader = GetParent()->GetShaderManager()->GetShader(desc.Path.c_str(), (ShaderType)i, desc.EntryPoint.c_str(), desc.Defines);
			if (!pShader)
			{
				shaderCompileError = true;
			}
			else
			{
				GetByteCode((ShaderType)i) = CD3DX12_SHADER_BYTECODE(pShader->pByteCode->GetBufferPointer(), pShader->pByteCode->GetBufferSize());
				if (name.empty())
					name = Sprintf("%s (Unnamed)", pShader->EntryPoint.c_str());
				m_Shaders[i] = pShader;
			}
		}
	}

	if (!shaderCompileError)
	{
		GetParent()->DeferReleaseObject(m_pPipelineState.Detach());
		D3D12_PIPELINE_STATE_STREAM_DESC streamDesc;
		streamDesc.SizeInBytes = sizeof(m_Desc.m_Stream);
		streamDesc.pPipelineStateSubobjectStream = &m_Desc.m_Stream;
		VERIFY_HR_EX(GetParent()->GetDevice()->CreatePipelineState(&streamDesc, IID_PPV_ARGS(m_pPipelineState.ReleaseAndGetAddressOf())), GetParent()->GetDevice());
		D3D::SetObjectName(m_pPipelineState.Get(), name.c_str());
	}
	else
	{
		E_LOG(Warning, "Failed to compile PipelineState '%s'", m_Desc.m_Name);
	}
	check(m_pPipelineState);
}

void PipelineState::ConditionallyReload()
{
	if (m_NeedsReload)
	{
		Create(m_Desc);
		m_NeedsReload = false;
		E_LOG(Info, "Reloaded Pipeline: %s", m_Desc.m_Name.c_str());
	}
}

void PipelineState::OnShaderReloaded(Shader* pShader)
{
	for (Shader*& pCurrentShader : m_Shaders)
	{
		if (pCurrentShader && pCurrentShader == pShader)
		{
			m_NeedsReload = true;
			break;
		}
	}
}
