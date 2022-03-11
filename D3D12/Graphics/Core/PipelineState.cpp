#include "stdafx.h"
#include "PipelineState.h"
#include "Shader.h"
#include "Graphics.h"
#include "RootSignature.h"

VertexElementLayout::VertexElementLayout(const VertexElementLayout& rhs)
	: m_ElementDesc(rhs.m_ElementDesc), m_SemanticNames(rhs.m_SemanticNames), m_NumElements(rhs.m_NumElements)
{
	FixupStrings();
}

VertexElementLayout& VertexElementLayout::operator=(const VertexElementLayout& rhs)
{
	m_ElementDesc = rhs.m_ElementDesc;
	m_SemanticNames = rhs.m_SemanticNames;
	m_NumElements = rhs.m_NumElements;
	FixupStrings();
	return *this;
}

void VertexElementLayout::AddVertexElement(const char* pSemantic, DXGI_FORMAT format, uint32 semanticIndex /*= 0*/, uint32 byteOffset /*= D3D12_APPEND_ALIGNED_ELEMENT*/, uint32 inputSlot /*= 0*/)
{
	check(strcpy_s(m_SemanticNames[m_NumElements], pSemantic) == 0);
	D3D12_INPUT_ELEMENT_DESC& element = m_ElementDesc[m_NumElements];
	element.AlignedByteOffset = byteOffset;
	element.Format = format;
	element.InputSlot = inputSlot;
	element.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
	element.InstanceDataStepRate = 0;
	element.SemanticIndex = semanticIndex;
	element.SemanticName = m_SemanticNames[m_NumElements];
	++m_NumElements;
}

void VertexElementLayout::AddInstanceElement(const char* pSemantic, DXGI_FORMAT format, uint32 semanticIndex, uint32 byteOffset, uint32 inputSlot, uint32 stepRate)
{
	check(strcpy_s(m_SemanticNames[m_NumElements], pSemantic) == 0);
	D3D12_INPUT_ELEMENT_DESC& element = m_ElementDesc[m_NumElements];
	element.AlignedByteOffset = byteOffset;
	element.Format = format;
	element.InputSlot = inputSlot;
	element.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
	element.InstanceDataStepRate = stepRate;
	element.SemanticIndex = semanticIndex;
	element.SemanticName = m_SemanticNames[m_NumElements];
	++m_NumElements;
}

void VertexElementLayout::FixupStrings()
{
	for (size_t i = 0; i < m_ElementDesc.size(); ++i)
	{
		m_ElementDesc[i].SemanticName = m_SemanticNames[i];
	}
}

PipelineStateInitializer::PipelineStateInitializer()
{
	m_pSubobjectData.resize(sizeof(CD3DX12_PIPELINE_STATE_STREAM2));
	m_pSubobjectLocations.fill(-1);

	GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND>() = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1>() = CD3DX12_DEPTH_STENCIL_DESC1(D3D12_DEFAULT);
	GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER>() = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC>() = DefaultSampleDesc();
	GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY>() = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS>() = D3D12_PIPELINE_STATE_FLAG_NONE;
	GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK>() = DefaultSampleMask();
}

void PipelineStateInitializer::SetName(const char* pName)
{
	m_Name = pName;
}

void PipelineStateInitializer::SetDepthOnlyTarget(DXGI_FORMAT dsvFormat, uint32 msaa)
{
	D3D12_RT_FORMAT_ARRAY& formatArray = GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS>();
	formatArray.NumRenderTargets = 0;
	DXGI_SAMPLE_DESC& sampleDesc = GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC>();
	sampleDesc.Count = msaa;
	sampleDesc.Quality = 0;
	GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER>().MultisampleEnable = msaa > 1;
	GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT>() = dsvFormat;
}

void PipelineStateInitializer::SetRenderTargetFormat(DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat, uint32 msaa)
{
	SetRenderTargetFormats(&rtvFormat, 1, dsvFormat, msaa);
}

void PipelineStateInitializer::SetRenderTargetFormats(const DXGI_FORMAT* rtvFormats, uint32 numRenderTargets, DXGI_FORMAT dsvFormat, uint32 msaa)
{
	D3D12_RT_FORMAT_ARRAY& formatArray = GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS>();
	for (uint32 i = 0; i < numRenderTargets; ++i)
	{
		formatArray.RTFormats[i] = rtvFormats[i];
	}
	formatArray.NumRenderTargets = numRenderTargets;
	DXGI_SAMPLE_DESC& sampleDesc = GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC>();
	sampleDesc.Count = msaa;
	sampleDesc.Quality = 0;
	GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER>().MultisampleEnable = msaa > 1;
	GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT>() = dsvFormat;
}

void PipelineStateInitializer::SetBlendMode(const BlendMode& blendMode, bool /*alphaToCoverage*/)
{
	D3D12_BLEND_DESC& blendDesc = GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND>();
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
	GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1>().DepthEnable = enabled;
}

void PipelineStateInitializer::SetDepthWrite(bool enabled)
{
	GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1>().DepthWriteMask = enabled ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
}

void PipelineStateInitializer::SetDepthTest(D3D12_COMPARISON_FUNC func)
{
	GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1>().DepthFunc = func;
}

void PipelineStateInitializer::SetStencilTest(bool stencilEnabled, D3D12_COMPARISON_FUNC mode, D3D12_STENCIL_OP pass, D3D12_STENCIL_OP fail, D3D12_STENCIL_OP zFail, unsigned int /*stencilRef*/, unsigned char compareMask, unsigned char writeMask)
{
	D3D12_DEPTH_STENCIL_DESC1& dssDesc = GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1>();
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
	GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER>().FillMode = fillMode;
}

void PipelineStateInitializer::SetCullMode(D3D12_CULL_MODE cullMode)
{
	GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER>().CullMode = cullMode;
}

void PipelineStateInitializer::SetLineAntialias(bool lineAntiAlias)
{
	GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER>().AntialiasedLineEnable = lineAntiAlias;
}

void PipelineStateInitializer::SetDepthBias(int depthBias, float depthBiasClamp, float slopeScaledDepthBias)
{
	D3D12_RASTERIZER_DESC& rsDesc = GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER>();
	rsDesc.SlopeScaledDepthBias = slopeScaledDepthBias;
	rsDesc.DepthBias = depthBias;
	rsDesc.DepthBiasClamp = depthBiasClamp;
}

void PipelineStateInitializer::SetInputLayout(const VertexElementLayout& layout)
{
	D3D12_INPUT_LAYOUT_DESC& ilDesc = GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT>();
	m_InputLayout = layout;
	ilDesc.NumElements = (uint32)m_InputLayout.GetNumElements();
	ilDesc.pInputElementDescs = m_InputLayout.GetElements();
}

void PipelineStateInitializer::SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE topology)
{
	GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY>() = topology;
}

void PipelineStateInitializer::SetRootSignature(RootSignature* pRootSignature)
{
	GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE>() = pRootSignature->GetRootSignature();
}

void PipelineStateInitializer::SetVertexShader(const char* pShaderPath, const char* entryPoint, const std::vector<ShaderDefine>& defines)
{
	m_Type = PipelineStateType::Graphics;
	m_ShaderDescs[(int)ShaderType::Vertex] = { pShaderPath, entryPoint, defines };
}

void PipelineStateInitializer::SetPixelShader(const char* pShaderPath, const char* entryPoint, const std::vector<ShaderDefine>& defines)
{
	m_ShaderDescs[(int)ShaderType::Pixel] = { pShaderPath, entryPoint, defines };
}

void PipelineStateInitializer::SetGeometryShader(const char* pShaderPath, const char* entryPoint, const std::vector<ShaderDefine>& defines)
{
	m_Type = PipelineStateType::Graphics;
	m_ShaderDescs[(int)ShaderType::Geometry] = { pShaderPath, entryPoint, defines };
}

void PipelineStateInitializer::SetComputeShader(const char* pShaderPath, const char* entryPoint, const std::vector<ShaderDefine>& defines)
{
	m_Type = PipelineStateType::Compute;
	m_ShaderDescs[(int)ShaderType::Compute] = { pShaderPath, entryPoint, defines };
}

void PipelineStateInitializer::SetMeshShader(const char* pShaderPath, const char* entryPoint, const std::vector<ShaderDefine>& defines)
{
	m_Type = PipelineStateType::Mesh;
	m_ShaderDescs[(int)ShaderType::Mesh] = { pShaderPath, entryPoint, defines };
}

void PipelineStateInitializer::SetAmplificationShader(const char* pShaderPath, const char* entryPoint, const std::vector<ShaderDefine>& defines)
{
	m_Type = PipelineStateType::Mesh;
	m_ShaderDescs[(int)ShaderType::Amplification] = { pShaderPath, entryPoint, defines };
}

D3D12_PIPELINE_STATE_STREAM_DESC PipelineStateInitializer::GetDesc(GraphicsDevice* pDevice)
{
	auto GetByteCode = [this](ShaderType type) -> D3D12_SHADER_BYTECODE& {
		switch (type)
		{
		case ShaderType::Vertex: return GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS>();
		case ShaderType::Pixel: return GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS>();
		case ShaderType::Geometry: return GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS>();
		case ShaderType::Mesh: return GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS>();
		case ShaderType::Amplification: return GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS>();
		case ShaderType::Compute: return GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS>();
		case ShaderType::MAX:
		default:
			noEntry();
			static D3D12_SHADER_BYTECODE dummy;
			return dummy;
		}
	};

	for (uint32 i = 0; i < (int)ShaderType::MAX; ++i)
	{
		Shader* pShader = nullptr;
		const ShaderDesc& desc = m_ShaderDescs[i];
		if (desc.Path.length() > 0)
		{
			pShader = pDevice->GetShaderManager()->GetShader(desc.Path.c_str(), (ShaderType)i, desc.EntryPoint.c_str(), desc.Defines);
			if (pShader)
			{
				GetByteCode((ShaderType)i) = D3D12_SHADER_BYTECODE{ pShader->GetByteCode(), pShader->GetByteCodeSize() };
				if (m_Name.empty())
				{
					m_Name = Sprintf("%s (Unnamed)", pShader->GetEntryPoint());
				}
			}

			m_Shaders[i] = pShader;
		}
	}

	D3D12_PIPELINE_STATE_STREAM_DESC streamDesc;
	streamDesc.pPipelineStateSubobjectStream = m_pSubobjectData.data();
	streamDesc.SizeInBytes = m_Size;

	return streamDesc;
}

std::string PipelineStateInitializer::DebugPrint()
{
	std::stringstream str;
	str << "---------------------------------\n";
	str << "| D3D12 Pipeline State Stream |\n";

	// Fix up the assigned shaders
	//GetDesc();

	for (uint32 i = 0; i < D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MAX_VALID; ++i)
	{
		int offset = m_pSubobjectLocations[i];
		if (offset >= 0)
		{
			str << "| [" << i << "]: ";

			struct SubobjectType
			{
				D3D12_PIPELINE_STATE_SUBOBJECT_TYPE ObjType;
				void* ObjectData;
			};
			SubobjectType* pSubObject = (SubobjectType*)&m_pSubobjectData[offset];
			void* pSubObjectData = &pSubObject->ObjectData;

			switch (pSubObject->ObjType)
			{
			case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS:
			{
				D3D12_PIPELINE_STATE_FLAGS* pObjectData = static_cast<D3D12_PIPELINE_STATE_FLAGS*>(pSubObjectData);
				str << "Flags: " << *pObjectData;
				break;
			}
			case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK:
			{
				UINT* pObjectData = static_cast<UINT*>(pSubObjectData);
				str << "Node Mask: " << *pObjectData;
				break;
			}
			case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE:
			{
				ID3D12RootSignature** pObjectData = static_cast<ID3D12RootSignature**>(pSubObjectData);
				str << "Root Signature: " << *pObjectData;
				break;
			}
			case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT:
			{
				D3D12_INPUT_LAYOUT_DESC* pObjectData = static_cast<D3D12_INPUT_LAYOUT_DESC*>(pSubObjectData);
				str << "Input Layout: \n";
				str << "\tElements: " << pObjectData->NumElements;
				for (uint32 elementIndex = 0; elementIndex < pObjectData->NumElements; ++elementIndex)
				{
					str << "\t[" << elementIndex << "] " << pObjectData->pInputElementDescs[elementIndex].SemanticName << pObjectData->pInputElementDescs[elementIndex].SemanticIndex;
				}
				break;
			}
			case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE:
			{
				D3D12_INDEX_BUFFER_STRIP_CUT_VALUE* pObjectData = static_cast<D3D12_INDEX_BUFFER_STRIP_CUT_VALUE*>(pSubObjectData);
				str << "Index Buffer Strip Cut Value: " << *pObjectData;
				break;
			}
			case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY:
			{
				D3D12_PRIMITIVE_TOPOLOGY_TYPE* pObjectData = static_cast<D3D12_PRIMITIVE_TOPOLOGY_TYPE*>(pSubObjectData);
				str << "Primitive Topology: " << *pObjectData;
				break;
			}
			case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS:
			{
				D3D12_SHADER_BYTECODE* pObjectData = static_cast<D3D12_SHADER_BYTECODE*>(pSubObjectData);
				str << "Vertex Shader - ByteCode: 0x" << pObjectData->pShaderBytecode << " - Length: " << pObjectData->BytecodeLength << " bytes";
				break;
			}
			case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS:
			{
				D3D12_SHADER_BYTECODE* pObjectData = static_cast<D3D12_SHADER_BYTECODE*>(pSubObjectData);
				str << "Geometry Shader - ByteCode: 0x" << pObjectData->pShaderBytecode << " - Length: " << pObjectData->BytecodeLength << " bytes";
				break;
			}
			case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS:
			{
				D3D12_SHADER_BYTECODE* pObjectData = static_cast<D3D12_SHADER_BYTECODE*>(pSubObjectData);
				str << "Pixel Shader - ByteCode: 0x" << pObjectData->pShaderBytecode << " - Length: " << pObjectData->BytecodeLength << " bytes";
				break;
			}
			case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS:
			{
				D3D12_SHADER_BYTECODE* pObjectData = static_cast<D3D12_SHADER_BYTECODE*>(pSubObjectData);
				str << "Compute Shader - ByteCode: 0x" << pObjectData->pShaderBytecode << " - Length: " << pObjectData->BytecodeLength << " bytes";
				break;
			}
			case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS:
			{
				D3D12_SHADER_BYTECODE* pObjectData = static_cast<D3D12_SHADER_BYTECODE*>(pSubObjectData);
				str << "Mesh Shader - ByteCode: 0x" << pObjectData->pShaderBytecode << " - Length: " << pObjectData->BytecodeLength << " bytes";
				break;
			}
			case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS:
			{
				D3D12_SHADER_BYTECODE* pObjectData = static_cast<D3D12_SHADER_BYTECODE*>(pSubObjectData);
				str << "Amplification Shader - ByteCode: 0x" << pObjectData->pShaderBytecode << " - Length: " << pObjectData->BytecodeLength << " bytes";
				break;
			}
			case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS:
			{
				D3D12_SHADER_BYTECODE* pObjectData = static_cast<D3D12_SHADER_BYTECODE*>(pSubObjectData);
				str << "Hull Shader - ByteCode: 0x" << pObjectData->pShaderBytecode << " - Length: " << pObjectData->BytecodeLength << " bytes";
				break;
			}
			case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS:
			{
				D3D12_SHADER_BYTECODE* pObjectData = static_cast<D3D12_SHADER_BYTECODE*>(pSubObjectData);
				str << "Domain Shader - ByteCode: 0x" << pObjectData->pShaderBytecode << " - Length: " << pObjectData->BytecodeLength << " bytes";
				break;
			}
			case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT:
			{
				//D3D12_STREAM_OUTPUT_DESC* pObjectData = static_cast<D3D12_STREAM_OUTPUT_DESC*>(pSubObjectData);
				str << "Stream Output";
				break;
			}
			case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND:
			{
				//CD3DX12_BLEND_DESC* pObjectData = static_cast<CD3DX12_BLEND_DESC*>(pSubObjectData);
				str << "Blend Desc";
				break;
			}
			default:
				break;
			}

			str << "\n";
		}
	}
	str << "|--------------------------------------------------------------------\n";
	return str.str();
}

PipelineState::PipelineState(GraphicsDevice* pParent)
	: GraphicsObject(pParent)
{
	m_ReloadHandle = pParent->GetShaderManager()->OnShaderRecompiledEvent().AddRaw(this, &PipelineState::OnShaderReloaded);
}

PipelineState::~PipelineState()
{
	GetParent()->GetShaderManager()->OnShaderRecompiledEvent().Remove(m_ReloadHandle);
}

void PipelineState::Create(const PipelineStateInitializer& initializer)
{
	check(initializer.m_Type != PipelineStateType::MAX);
	RefCountPtr<ID3D12Device2> pDevice2;
	VERIFY_HR_EX(GetParent()->GetDevice()->QueryInterface(IID_PPV_ARGS(pDevice2.GetAddressOf())), GetParent()->GetDevice());

	m_Desc = initializer;
	if (initializer.m_InputLayout.GetNumElements() > 0)
	{
		D3D12_INPUT_LAYOUT_DESC& ilDesc = m_Desc.GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT>();
		ilDesc.pInputElementDescs = m_Desc.m_InputLayout.GetElements();
	}

	D3D12_PIPELINE_STATE_STREAM_DESC streamDesc = m_Desc.GetDesc(GetParent());
	VERIFY_HR_EX(pDevice2->CreatePipelineState(&streamDesc, IID_PPV_ARGS(m_pPipelineState.ReleaseAndGetAddressOf())), GetParent()->GetDevice());
	D3D::SetObjectName(m_pPipelineState.Get(), m_Desc.m_Name.c_str());
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

void PipelineState::OnShaderReloaded(Shader* pOldShader, Shader* pNewShader)
{
	for (Shader*& pShader : m_Desc.m_Shaders)
	{
		if (pShader && pShader == pOldShader)
		{
			pShader = pNewShader;
			m_NeedsReload = true;
		}
	}
}
