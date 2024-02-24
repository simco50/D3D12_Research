#include "stdafx.h"
#include "RootSignature.h"
#include "Shader.h"
#include "Graphics.h"

static D3D12_DESCRIPTOR_RANGE_FLAGS sDefaultTableRangeFlags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE | D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
static D3D12_ROOT_DESCRIPTOR_FLAGS sDefaultRootDescriptorFlags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;

RootSignature::RootSignature(GraphicsDevice* pParent)
	: GraphicsObject(pParent),
	m_NumParameters(0)
{
}

void RootSignature::AddRootConstants(uint32 shaderRegister, uint32 constantCount, uint32 space, D3D12_SHADER_VISIBILITY visibility)
{
	RootParameter& parameter = m_RootParameters[m_NumParameters++];
	parameter.Data.InitAsConstants(constantCount, shaderRegister, space, visibility);
}

void RootSignature::AddRootCBV(uint32 shaderRegister, uint32 space, D3D12_SHADER_VISIBILITY visibility)
{
	RootParameter& parameter = m_RootParameters[m_NumParameters++];
	parameter.Data.InitAsConstantBufferView(shaderRegister, space, sDefaultRootDescriptorFlags, visibility);
}

void RootSignature::AddRootSRV(uint32 shaderRegister, uint32 space, D3D12_SHADER_VISIBILITY visibility)
{
	RootParameter& parameter = m_RootParameters[m_NumParameters++];
	parameter.Data.InitAsShaderResourceView(shaderRegister, space, sDefaultRootDescriptorFlags, visibility);
}

void RootSignature::AddRootUAV(uint32 shaderRegister, uint32 space, D3D12_SHADER_VISIBILITY visibility)
{
	RootParameter& parameter = m_RootParameters[m_NumParameters++];
	parameter.Data.InitAsUnorderedAccessView(shaderRegister, space, sDefaultRootDescriptorFlags, visibility);
}

void RootSignature::AddDescriptorTable(uint32 shaderRegister, uint32 numDescriptors, D3D12_DESCRIPTOR_RANGE_TYPE type, uint32 space, D3D12_SHADER_VISIBILITY visibility)
{
	RootParameter& parameter = m_RootParameters[m_NumParameters++];
	parameter.Range.Init(type, numDescriptors, shaderRegister, space, sDefaultTableRangeFlags, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND);
	parameter.Data.InitAsDescriptorTable(1, &parameter.Range, visibility);
}

void RootSignature::AddStaticSampler(uint32 registerSlot, D3D12_FILTER filter, D3D12_TEXTURE_ADDRESS_MODE wrapMode, D3D12_COMPARISON_FUNC compareFunc)
{
	D3D12_STATIC_SAMPLER_DESC desc{};
	desc.AddressU = wrapMode;
	desc.AddressV = wrapMode;
	desc.AddressW = wrapMode;
	desc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
	desc.ComparisonFunc = compareFunc;
	desc.Filter = filter;
	desc.MaxAnisotropy = 8;
	desc.MaxLOD = FLT_MAX;
	desc.MinLOD = 0.0f;
	desc.RegisterSpace = 1;
	desc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	desc.ShaderRegister = registerSlot;
	desc.MipLODBias = 0.0f;
	m_StaticSamplers.push_back(desc);
}

void RootSignature::Finalize(const char* pName, D3D12_ROOT_SIGNATURE_FLAGS flags)
{
	D3D12_ROOT_SIGNATURE_FLAGS visibilityFlags =
		D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS
		| D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS
		| D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS
		| D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS
		| D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

	if (!GetParent()->GetCapabilities().SupportsMeshShading())
	{
		visibilityFlags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS;
		visibilityFlags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS;
	}

	int staticSamplerRegisterSlot = 0;
	AddStaticSampler(staticSamplerRegisterSlot++, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP);
	AddStaticSampler(staticSamplerRegisterSlot++, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
	AddStaticSampler(staticSamplerRegisterSlot++, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_BORDER);

	AddStaticSampler(staticSamplerRegisterSlot++, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_WRAP);
	AddStaticSampler(staticSamplerRegisterSlot++, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
	AddStaticSampler(staticSamplerRegisterSlot++, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_BORDER);

	AddStaticSampler(staticSamplerRegisterSlot++, D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_BORDER);
	AddStaticSampler(staticSamplerRegisterSlot++, D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_BORDER);
	AddStaticSampler(staticSamplerRegisterSlot++, D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_BORDER);

	AddStaticSampler(staticSamplerRegisterSlot++, D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_WRAP);
	AddStaticSampler(staticSamplerRegisterSlot++, D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_COMPARISON_FUNC_GREATER);
	AddStaticSampler(staticSamplerRegisterSlot++, D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_COMPARISON_FUNC_GREATER);

	std::array<D3D12_ROOT_PARAMETER1, sMaxNumParameters> rootParameters;
	for (size_t i = 0; i < m_NumParameters; ++i)
	{
		RootParameter& rootParameter = m_RootParameters[i];
		switch (rootParameter.Data.ShaderVisibility)
		{
		case D3D12_SHADER_VISIBILITY_VERTEX:		visibilityFlags = visibilityFlags & ~D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS;			break;
		case D3D12_SHADER_VISIBILITY_GEOMETRY:		visibilityFlags = visibilityFlags & ~D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;		break;
		case D3D12_SHADER_VISIBILITY_PIXEL:			visibilityFlags = visibilityFlags & ~D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;			break;
		case D3D12_SHADER_VISIBILITY_HULL:			visibilityFlags = visibilityFlags & ~D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS;			break;
		case D3D12_SHADER_VISIBILITY_DOMAIN:		visibilityFlags = visibilityFlags & ~D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS;			break;
		case D3D12_SHADER_VISIBILITY_MESH:			visibilityFlags = visibilityFlags & ~D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS;			break;
		case D3D12_SHADER_VISIBILITY_AMPLIFICATION:	visibilityFlags = visibilityFlags & ~D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS;	break;
		case D3D12_SHADER_VISIBILITY_ALL:			visibilityFlags = D3D12_ROOT_SIGNATURE_FLAG_NONE;														break;
		default:									noEntry();																								break;
		}

		rootParameters[i] = rootParameter.Data;
	}

	if (!EnumHasAnyFlags(flags, D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE))
	{
		flags |= visibilityFlags;
		flags |= D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
		flags |= D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;
	}

	constexpr uint32 recommendedDwords = 12;
	uint32 dwords = GetDWORDSize();
	if (dwords > recommendedDwords)
	{
		E_LOG(Warning, "[RootSignature::Finalize] RootSignature '%s' uses %d DWORDs while under %d is recommended", pName, dwords, recommendedDwords);
	}

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC desc = {};
	desc.Init_1_1(m_NumParameters, rootParameters.data(), (uint32)m_StaticSamplers.size(), m_StaticSamplers.data(), flags);
	Ref<ID3DBlob> pDataBlob, pErrorBlob;
	D3D12SerializeVersionedRootSignature(&desc, pDataBlob.GetAddressOf(), pErrorBlob.GetAddressOf());
	if (pErrorBlob)
	{
		const char* pError = (char*)pErrorBlob->GetBufferPointer();
		E_LOG(Error, "RootSignature serialization error: %s", pError);
		return;
	}
	VERIFY_HR_EX(GetParent()->GetDevice()->CreateRootSignature(0, pDataBlob->GetBufferPointer(), pDataBlob->GetBufferSize(), IID_PPV_ARGS(m_pRootSignature.ReleaseAndGetAddressOf())), GetParent()->GetDevice());
	D3D::SetObjectName(m_pRootSignature.Get(), pName);
}

uint32 RootSignature::GetDescriptorTableSize(uint32 rootIndex) const
{
	check(rootIndex < m_NumParameters);
	const RootParameter& parameter = m_RootParameters[rootIndex];
	if (parameter.Data.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
		return parameter.Range.NumDescriptors;
	return 0;
}

uint32 RootSignature::GetDWORDSize() const
{
	uint32 count = 0;
	for (size_t i = 0; i < m_NumParameters; ++i)
	{
		const RootParameter& rootParameter = m_RootParameters[i];
		switch (rootParameter.Data.ParameterType)
		{
		case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
			count += rootParameter.Data.Constants.Num32BitValues;
			break;
		case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
			count += 1;
			break;
		case D3D12_ROOT_PARAMETER_TYPE_CBV:
		case D3D12_ROOT_PARAMETER_TYPE_SRV:
		case D3D12_ROOT_PARAMETER_TYPE_UAV:
			count += 2;
			break;
		}
	}
	return count;
}
