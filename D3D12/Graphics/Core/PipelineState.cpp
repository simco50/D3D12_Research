#include "stdafx.h"
#include "PipelineState.h"
#include "Shader.h"

StateObjectDesc::StateObjectDesc(D3D12_STATE_OBJECT_TYPE type /*= D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE*/)
	: m_StateObjectAllocator(0xFF), m_ScratchAllocator(0xFFFF), m_Type(type)
{

}

uint32 StateObjectDesc::AddLibrary(const ShaderLibrary& shader, const std::vector<std::string>& exports /*= {}*/)
{
	D3D12_DXIL_LIBRARY_DESC* pDesc = m_ScratchAllocator.Allocate<D3D12_DXIL_LIBRARY_DESC>();
	pDesc->DXILLibrary.BytecodeLength = shader.GetByteCodeSize();
	pDesc->DXILLibrary.pShaderBytecode = shader.GetByteCode();
	if (exports.size())
	{
		D3D12_EXPORT_DESC* pExports = m_ScratchAllocator.Allocate<D3D12_EXPORT_DESC>((uint32)exports.size());
		D3D12_EXPORT_DESC* pCurrentExport = pExports;
		for (const std::string& exportName : exports)
		{
			uint32 len = (uint32)exportName.length();
			wchar_t* pNameData = m_ScratchAllocator.Allocate<wchar_t>(len + 1);
			MultiByteToWideChar(0, 0, exportName.c_str(), len, pNameData, len);
			pCurrentExport->ExportToRename = pNameData;
			pCurrentExport->Name = pNameData;
			pCurrentExport->Flags = D3D12_EXPORT_FLAG_NONE;
			pCurrentExport++;
		}
		pDesc->NumExports = (uint32)exports.size();
		pDesc->pExports = pExports;
	}
	return AddStateObject(pDesc, D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY);
}

uint32 StateObjectDesc::AddHitGroup(const char* pHitGroupExport, const char* pClosestHitShaderImport /*= nullptr*/, const char* pAnyHitShaderImport /*= nullptr*/, const char* pIntersectionShaderImport /*= nullptr*/)
{
	check(pHitGroupExport);
	D3D12_HIT_GROUP_DESC* pDesc = m_ScratchAllocator.Allocate<D3D12_HIT_GROUP_DESC>();
	{
		int len = (int)strlen(pHitGroupExport);
		wchar_t* pNameData = m_ScratchAllocator.Allocate<wchar_t>(len + 1);
		MultiByteToWideChar(0, 0, pHitGroupExport, len, pNameData, len);
		pDesc->HitGroupExport = pNameData;
	}
	if (pClosestHitShaderImport)
	{
		uint32 len = (uint32)strlen(pClosestHitShaderImport);
		wchar_t* pNameData = m_ScratchAllocator.Allocate<wchar_t>(len + 1);
		MultiByteToWideChar(0, 0, pClosestHitShaderImport, len, pNameData, len);
		pDesc->ClosestHitShaderImport = pNameData;
	}
	if (pAnyHitShaderImport)
	{
		uint32 len = (uint32)strlen(pAnyHitShaderImport);
		wchar_t* pNameData = m_ScratchAllocator.Allocate<wchar_t>(len + 1);
		MultiByteToWideChar(0, 0, pAnyHitShaderImport, len, pNameData, len);
		pDesc->AnyHitShaderImport = pNameData;
	}
	if (pIntersectionShaderImport)
	{
		uint32 len = (uint32)strlen(pIntersectionShaderImport);
		wchar_t* pNameData = m_ScratchAllocator.Allocate<wchar_t>(len + 1);
		MultiByteToWideChar(0, 0, pIntersectionShaderImport, len, pNameData, len);
		pDesc->HitGroupExport = pNameData;
	}
	pDesc->Type = pIntersectionShaderImport ? D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE : D3D12_HIT_GROUP_TYPE_TRIANGLES;
	return AddStateObject(pDesc, D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP);
}

uint32 StateObjectDesc::AddStateAssociation(uint32 index, const std::vector<std::string>& exports)
{
	check(exports.size());
	D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION* pAssociation = m_ScratchAllocator.Allocate<D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION>();
	pAssociation->NumExports = (uint32)exports.size();
	pAssociation->pSubobjectToAssociate = GetSubobject(index);
	const wchar_t** pExportList = m_ScratchAllocator.Allocate<const wchar_t*>(pAssociation->NumExports);
	pAssociation->pExports = pExportList;
	for (size_t i = 0; i < exports.size(); ++i)
	{
		uint32 len = (uint32)exports[i].length();
		wchar_t* pNameData = m_ScratchAllocator.Allocate<wchar_t>(len + 1);
		MultiByteToWideChar(0, 0, exports[i].c_str(), len, pNameData, len);
		pExportList[i] = pNameData;
	}
	return AddStateObject(pAssociation, D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION);
}

uint32 StateObjectDesc::AddCollection(ID3D12StateObject* pStateObject, const std::vector<std::string>& exports /*= {}*/)
{
	D3D12_EXISTING_COLLECTION_DESC* pDesc = m_ScratchAllocator.Allocate<D3D12_EXISTING_COLLECTION_DESC>();
	pDesc->pExistingCollection = pStateObject;
	if (exports.size())
	{
		D3D12_EXPORT_DESC* pExports = m_ScratchAllocator.Allocate<D3D12_EXPORT_DESC>((uint32)exports.size());
		pDesc->pExports = pExports;
		for (size_t i = 0; i < exports.size(); ++i)
		{
			D3D12_EXPORT_DESC& currentExport = pExports[i];
			uint32 len = (uint32)exports[i].length();
			wchar_t* pNameData = m_ScratchAllocator.Allocate<wchar_t>(len + 1);
			MultiByteToWideChar(0, 0, exports[i].c_str(), len, pNameData, len);
			currentExport.ExportToRename = pNameData;
			currentExport.Name = pNameData;
			currentExport.Flags = D3D12_EXPORT_FLAG_NONE;
		}
	}
	return AddStateObject(pDesc, D3D12_STATE_SUBOBJECT_TYPE_EXISTING_COLLECTION);
}

uint32 StateObjectDesc::BindLocalRootSignature(const char* pExportName, ID3D12RootSignature* pRootSignature)
{
	D3D12_LOCAL_ROOT_SIGNATURE* pRs = m_ScratchAllocator.Allocate<D3D12_LOCAL_ROOT_SIGNATURE>();
	pRs->pLocalRootSignature = pRootSignature;
	uint32 rsState = AddStateObject(pRs, D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE);
	return AddStateAssociation(rsState, { pExportName });
}

uint32 StateObjectDesc::SetRaytracingShaderConfig(uint32 maxPayloadSize, uint32 maxAttributeSize)
{
	D3D12_RAYTRACING_SHADER_CONFIG* pDesc = m_ScratchAllocator.Allocate<D3D12_RAYTRACING_SHADER_CONFIG>();
	pDesc->MaxPayloadSizeInBytes = maxPayloadSize;
	pDesc->MaxAttributeSizeInBytes = maxAttributeSize;
	return AddStateObject(pDesc, D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG);
}

uint32 StateObjectDesc::SetRaytracingPipelineConfig(uint32 maxRecursionDepth)
{
	D3D12_RAYTRACING_PIPELINE_CONFIG1* pDesc = m_ScratchAllocator.Allocate<D3D12_RAYTRACING_PIPELINE_CONFIG1>();
	pDesc->MaxTraceRecursionDepth = maxRecursionDepth;
	return AddStateObject(pDesc, D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG);
}

uint32 StateObjectDesc::SetGlobalRootSignature(ID3D12RootSignature* pRootSignature)
{
	D3D12_GLOBAL_ROOT_SIGNATURE* pRs = m_ScratchAllocator.Allocate<D3D12_GLOBAL_ROOT_SIGNATURE>();
	pRs->pGlobalRootSignature = pRootSignature;
	return AddStateObject(pRs, D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE);
}

ComPtr<ID3D12StateObject> StateObjectDesc::Finalize(const char* pName, ID3D12Device5* pDevice) const
{
	D3D12_STATE_OBJECT_DESC desc;
	desc.NumSubobjects = m_SubObjects;
	desc.Type = m_Type;
	desc.pSubobjects = (D3D12_STATE_SUBOBJECT*)m_StateObjectAllocator.Data();
	ComPtr<ID3D12StateObject> pStateObject;
	VERIFY_HR(pDevice->CreateStateObject(&desc, IID_PPV_ARGS(pStateObject.GetAddressOf())));
	return pStateObject;
}

uint32 StateObjectDesc::AddStateObject(void* pDesc, D3D12_STATE_SUBOBJECT_TYPE type)
{
	D3D12_STATE_SUBOBJECT* pState = m_StateObjectAllocator.Allocate<D3D12_STATE_SUBOBJECT>();
	pState->pDesc = pDesc;
	pState->Type = type;
	return m_SubObjects++;
}

PipelineState::PipelineState()
{
	*m_Desc.GetSubobjectData<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND>() = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	*m_Desc.GetSubobjectData<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1>() = CD3DX12_DEPTH_STENCIL_DESC1(D3D12_DEFAULT);
	*m_Desc.GetSubobjectData<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER>() = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	*m_Desc.GetSubobjectData<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC>() = DefaultSampleDesc();
	*m_Desc.GetSubobjectData<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY>() = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	*m_Desc.GetSubobjectData<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS>() = D3D12_PIPELINE_STATE_FLAG_NONE;
	*m_Desc.GetSubobjectData<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK>() = DefaultSampleMask();
}

PipelineState::PipelineState(const PipelineState& other)
	: m_Desc(other.m_Desc),
	m_Type(other.m_Type)
{
}

void PipelineState::Finalize(const char* pName, ID3D12Device* pDevice)
{
	check(m_Type != PipelineStateType::MAX);
	ComPtr<ID3D12Device2> pDevice2;
	VERIFY_HR_EX(pDevice->QueryInterface(IID_PPV_ARGS(pDevice2.GetAddressOf())), pDevice);
	D3D12_PIPELINE_STATE_STREAM_DESC streamDesc = m_Desc.Desc();
	VERIFY_HR_EX(pDevice2->CreatePipelineState(&streamDesc, IID_PPV_ARGS(m_pPipelineState.GetAddressOf())), pDevice);
	D3D::SetObjectName(m_pPipelineState.Get(), pName);
}

void PipelineState::SetRenderTargetFormat(DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat, uint32 msaa)
{
	SetRenderTargetFormats(&rtvFormat, 1, dsvFormat, msaa);
}

void PipelineState::SetRenderTargetFormats(DXGI_FORMAT* rtvFormats, uint32 count, DXGI_FORMAT dsvFormat, uint32 msaa)
{
	D3D12_RT_FORMAT_ARRAY* pFormatArray = m_Desc.GetSubobjectData<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS>();
	pFormatArray->NumRenderTargets = count;
	for (uint32 i = 0; i < count; ++i)
	{
		pFormatArray->RTFormats[i] = rtvFormats[i];
	}
	DXGI_SAMPLE_DESC* pSampleDesc = m_Desc.GetSubobjectData<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC>();
	pSampleDesc->Count = msaa;
	pSampleDesc->Quality = 0;

	DXGI_FORMAT* pDSVFormat = m_Desc.GetSubobjectData<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT>();
	*pDSVFormat = dsvFormat;
}

void PipelineState::SetBlendMode(const BlendMode& blendMode, bool /*alphaToCoverage*/)
{
	D3D12_BLEND_DESC* pBlendDesc = m_Desc.GetSubobjectData<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND>();
	D3D12_RENDER_TARGET_BLEND_DESC& desc = pBlendDesc->RenderTarget[0];
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
	D3D12_DEPTH_STENCIL_DESC1* pDssDesc = m_Desc.GetSubobjectData<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1>();
	pDssDesc->DepthEnable = enabled;
}

void PipelineState::SetDepthWrite(bool enabled)
{
	D3D12_DEPTH_STENCIL_DESC1* pDssDesc = m_Desc.GetSubobjectData<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1>();
	pDssDesc->DepthWriteMask = enabled ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
}

void PipelineState::SetDepthTest(const D3D12_COMPARISON_FUNC func)
{
	D3D12_DEPTH_STENCIL_DESC1* pDssDesc = m_Desc.GetSubobjectData<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1>();
	pDssDesc->DepthFunc = func;
}

void PipelineState::SetStencilTest(bool stencilEnabled, D3D12_COMPARISON_FUNC mode, D3D12_STENCIL_OP pass, D3D12_STENCIL_OP fail, D3D12_STENCIL_OP zFail, unsigned int /*stencilRef*/, unsigned char compareMask, unsigned char writeMask)
{
	D3D12_DEPTH_STENCIL_DESC1* pDssDesc = m_Desc.GetSubobjectData<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1>();
	pDssDesc->StencilEnable = stencilEnabled;
	pDssDesc->FrontFace.StencilFunc = mode;
	pDssDesc->FrontFace.StencilPassOp = pass;
	pDssDesc->FrontFace.StencilFailOp = fail;
	pDssDesc->FrontFace.StencilDepthFailOp = zFail;
	pDssDesc->StencilReadMask = compareMask;
	pDssDesc->StencilWriteMask = writeMask;
	pDssDesc->BackFace = pDssDesc->FrontFace;
}

void PipelineState::SetFillMode(D3D12_FILL_MODE fillMode)
{
	D3D12_RASTERIZER_DESC* pRsDesc = m_Desc.GetSubobjectData<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER>();
	pRsDesc->FillMode = fillMode;
}

void PipelineState::SetCullMode(D3D12_CULL_MODE cullMode)
{
	D3D12_RASTERIZER_DESC* pRsDesc = m_Desc.GetSubobjectData<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER>();
	pRsDesc->CullMode = cullMode;
}

void PipelineState::SetLineAntialias(bool lineAntiAlias)
{
	D3D12_RASTERIZER_DESC* pRsDesc = m_Desc.GetSubobjectData<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER>();
	pRsDesc->AntialiasedLineEnable = lineAntiAlias;
}

void PipelineState::SetDepthBias(int depthBias, float depthBiasClamp, float slopeScaledDepthBias)
{
	D3D12_RASTERIZER_DESC* pRsDesc = m_Desc.GetSubobjectData<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER>();
	pRsDesc->SlopeScaledDepthBias = slopeScaledDepthBias;
	pRsDesc->DepthBias = depthBias;
	pRsDesc->DepthBiasClamp = depthBiasClamp;
}

void PipelineState::SetInputLayout(D3D12_INPUT_ELEMENT_DESC* pElements, uint32 count)
{
	D3D12_INPUT_LAYOUT_DESC* pIlDesc = m_Desc.GetSubobjectData<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT>();
	pIlDesc->NumElements = count;
	pIlDesc->pInputElementDescs = pElements;
}

void PipelineState::SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE topology)
{
	D3D12_PRIMITIVE_TOPOLOGY_TYPE* pTopology = m_Desc.GetSubobjectData<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY>();
	*pTopology = topology;
}

void PipelineState::SetRootSignature(ID3D12RootSignature* pRootSignature)
{
	ID3D12RootSignature** pRs = m_Desc.GetSubobjectData<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE>();
	*pRs = pRootSignature;
}

void PipelineState::SetVertexShader(const Shader& shader)
{
	m_Type = PipelineStateType::Graphics;

	D3D12_SHADER_BYTECODE* pByteCode = m_Desc.GetSubobjectData<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS>();
	*pByteCode = { shader.GetByteCode(), shader.GetByteCodeSize() };
}

void PipelineState::SetPixelShader(const Shader& shader)
{
	D3D12_SHADER_BYTECODE* pByteCode = m_Desc.GetSubobjectData<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS>();
	*pByteCode = { shader.GetByteCode(), shader.GetByteCodeSize() };
}

void PipelineState::SetHullShader(const Shader& shader)
{
	m_Type = PipelineStateType::Graphics;
	D3D12_SHADER_BYTECODE* pByteCode = m_Desc.GetSubobjectData<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS>();
	*pByteCode = { shader.GetByteCode(), shader.GetByteCodeSize() };
}

void PipelineState::SetDomainShader(const Shader& shader)
{
	m_Type = PipelineStateType::Graphics;
	D3D12_SHADER_BYTECODE* pByteCode = m_Desc.GetSubobjectData<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS>();
	*pByteCode = { shader.GetByteCode(), shader.GetByteCodeSize() };
}

void PipelineState::SetGeometryShader(const Shader& shader)
{
	m_Type = PipelineStateType::Graphics;
	D3D12_SHADER_BYTECODE* pByteCode = m_Desc.GetSubobjectData<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS>();
	*pByteCode = { shader.GetByteCode(), shader.GetByteCodeSize() };
}

void PipelineState::SetComputeShader(const Shader& shader)
{
	m_Type = PipelineStateType::Compute;
	D3D12_SHADER_BYTECODE* pByteCode = m_Desc.GetSubobjectData<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS>();
	pByteCode->pShaderBytecode = shader.GetByteCode();
	pByteCode->BytecodeLength = shader.GetByteCodeSize();
}

void PipelineState::SetMeshShader(const Shader& shader)
{
	m_Type = PipelineStateType::Mesh;
	D3D12_SHADER_BYTECODE* pByteCode = m_Desc.GetSubobjectData<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS>();
	*pByteCode = { shader.GetByteCode(), shader.GetByteCodeSize() };
}

void PipelineState::SetAmplificationShader(const Shader& shader)
{
	m_Type = PipelineStateType::Mesh;
	D3D12_SHADER_BYTECODE* pByteCode = m_Desc.GetSubobjectData<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS>();
	*pByteCode = { shader.GetByteCode(), shader.GetByteCodeSize() };
}
