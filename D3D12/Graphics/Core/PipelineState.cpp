#include "stdafx.h"
#include "PipelineState.h"
#include "Shader.h"

StateObjectDesc::StateObjectDesc(D3D12_STATE_OBJECT_TYPE type /*= D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE*/)
	: m_ScratchAllocator(0xFFFF), m_StateObjectAllocator(0xFF), m_Type(type)
{

}

uint32 StateObjectDesc::AddLibrary(const void* pByteCode, uint32 byteCodeLength, const std::vector<std::string>& exports /*= {}*/)
{
	D3D12_DXIL_LIBRARY_DESC* pDesc = m_ScratchAllocator.Allocate<D3D12_DXIL_LIBRARY_DESC>();
	pDesc->DXILLibrary.BytecodeLength = byteCodeLength;
	pDesc->DXILLibrary.pShaderBytecode = pByteCode;
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

uint32 StateObjectDesc::SetRaytracingPipelineConfig(uint32 maxRecursionDepth, D3D12_RAYTRACING_PIPELINE_FLAGS flags)
{
	D3D12_RAYTRACING_PIPELINE_CONFIG1* pDesc = m_ScratchAllocator.Allocate<D3D12_RAYTRACING_PIPELINE_CONFIG1>();
	pDesc->MaxTraceRecursionDepth = maxRecursionDepth;
	pDesc->Flags = flags;
	return AddStateObject(pDesc, D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG1);
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
	m_Desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	m_Desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC1(D3D12_DEFAULT);
	m_Desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	m_Desc.SampleDesc = DefaultSampleDesc();
	m_Desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	m_Desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	m_Desc.SampleMask = DefaultSampleMask();
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
	D3D12_PIPELINE_STATE_STREAM_DESC streamDesc{};
	streamDesc.pPipelineStateSubobjectStream = &m_Desc;
	streamDesc.SizeInBytes = sizeof(m_Desc);
	VERIFY_HR_EX(pDevice2->CreatePipelineState(&streamDesc, IID_PPV_ARGS(m_pPipelineState.GetAddressOf())), pDevice);
	D3D::SetObjectName(m_pPipelineState.Get(), pName);
}

void PipelineState::SetRenderTargetFormat(DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat, uint32 msaa)
{
	SetRenderTargetFormats(&rtvFormat, 1, dsvFormat, msaa);
}

void PipelineState::SetRenderTargetFormats(DXGI_FORMAT* rtvFormats, uint32 count, DXGI_FORMAT dsvFormat, uint32 msaa)
{
	D3D12_RT_FORMAT_ARRAY* pFormatArray = &m_Desc.RTVFormats;
	pFormatArray->NumRenderTargets = count;
	for (uint32 i = 0; i < count; ++i)
	{
		pFormatArray->RTFormats[i] = rtvFormats[i];
	}
	DXGI_SAMPLE_DESC* pSampleDesc = &m_Desc.SampleDesc;
	pSampleDesc->Count = msaa;
	pSampleDesc->Quality = 0;
	m_Desc.DSVFormat = dsvFormat;
}

void PipelineState::SetBlendMode(const BlendMode& blendMode, bool /*alphaToCoverage*/)
{
	CD3DX12_BLEND_DESC* pBlendDesc = &m_Desc.BlendState;
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
	CD3DX12_DEPTH_STENCIL_DESC1* pDssDesc = &m_Desc.DepthStencilState;
	pDssDesc->DepthEnable = enabled;
}

void PipelineState::SetDepthWrite(bool enabled)
{
	CD3DX12_DEPTH_STENCIL_DESC1* pDssDesc = &m_Desc.DepthStencilState;
	pDssDesc->DepthWriteMask = enabled ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
}

void PipelineState::SetDepthTest(const D3D12_COMPARISON_FUNC func)
{
	CD3DX12_DEPTH_STENCIL_DESC1* pDssDesc = &m_Desc.DepthStencilState;
	pDssDesc->DepthFunc = func;
}

void PipelineState::SetStencilTest(bool stencilEnabled, D3D12_COMPARISON_FUNC mode, D3D12_STENCIL_OP pass, D3D12_STENCIL_OP fail, D3D12_STENCIL_OP zFail, unsigned int /*stencilRef*/, unsigned char compareMask, unsigned char writeMask)
{
	CD3DX12_DEPTH_STENCIL_DESC1* pDssDesc = &m_Desc.DepthStencilState;
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
	CD3DX12_RASTERIZER_DESC* pRsDesc = &m_Desc.RasterizerState;
	pRsDesc->FillMode = fillMode;
}

void PipelineState::SetCullMode(D3D12_CULL_MODE cullMode)
{
	CD3DX12_RASTERIZER_DESC* pRsDesc = &m_Desc.RasterizerState;
	pRsDesc->CullMode = cullMode;
}

void PipelineState::SetLineAntialias(bool lineAntiAlias)
{
	CD3DX12_RASTERIZER_DESC* pRsDesc = &m_Desc.RasterizerState;
	pRsDesc->AntialiasedLineEnable = lineAntiAlias;
}

void PipelineState::SetDepthBias(int depthBias, float depthBiasClamp, float slopeScaledDepthBias)
{
	CD3DX12_RASTERIZER_DESC* pRsDesc = &m_Desc.RasterizerState;
	pRsDesc->SlopeScaledDepthBias = slopeScaledDepthBias;
	pRsDesc->DepthBias = depthBias;
	pRsDesc->DepthBiasClamp = depthBiasClamp;
}

void PipelineState::SetInputLayout(D3D12_INPUT_ELEMENT_DESC* pElements, uint32 count)
{
	D3D12_INPUT_LAYOUT_DESC* pIlDesc = &m_Desc.InputLayout;
	pIlDesc->NumElements = count;
	pIlDesc->pInputElementDescs = pElements;
}

void PipelineState::SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE topology)
{
	m_Desc.PrimitiveTopologyType = topology;
}

void PipelineState::SetRootSignature(ID3D12RootSignature* pRootSignature)
{
	m_Desc.pRootSignature = pRootSignature;
}

void PipelineState::SetVertexShader(const void* pByteCode, uint32 byteCodeLength)
{
	m_Type = PipelineStateType::Graphics;
	m_Desc.VS = { pByteCode, byteCodeLength };
}

void PipelineState::SetPixelShader(const void* pByteCode, uint32 byteCodeLength)
{
	m_Desc.PS = { pByteCode, byteCodeLength };
}

void PipelineState::SetHullShader(const void* pByteCode, uint32 byteCodeLength)
{
	m_Type = PipelineStateType::Graphics;
	m_Desc.HS = { pByteCode, byteCodeLength };
}

void PipelineState::SetDomainShader(const void* pByteCode, uint32 byteCodeLength)
{
	m_Type = PipelineStateType::Graphics;
	m_Desc.DS = { pByteCode, byteCodeLength };
}

void PipelineState::SetGeometryShader(const void* pByteCode, uint32 byteCodeLength)
{
	m_Type = PipelineStateType::Graphics;
	m_Desc.GS = { pByteCode, byteCodeLength };
}

void PipelineState::SetComputeShader(const void* pByteCode, uint32 byteCodeLength)
{
	m_Type = PipelineStateType::Compute;
	m_Desc.CS = { pByteCode, byteCodeLength };
}

void PipelineState::SetMeshShader(const void* pByteCode, uint32 byteCodeLength)
{
	m_Type = PipelineStateType::Mesh;
	m_Desc.MS = { pByteCode, byteCodeLength };
}

void PipelineState::SetAmplificationShader(const void* pByteCode, uint32 byteCodeLength)
{
	m_Type = PipelineStateType::Mesh;
	m_Desc.AS = { pByteCode, byteCodeLength };
}
