#include "stdafx.h"
#include "RootSignature.h"
#include "Shader.h"
#include "Graphics.h"

RootSignature::RootSignature(GraphicsDevice* pParent)
	: GraphicsObject(pParent),
	m_NumParameters(0)
{

}

uint32 RootSignature::AddRootConstants(uint32 shaderRegister, uint32 constantCount, D3D12_SHADER_VISIBILITY visibility)
{
	uint32 rootIndex = m_NumParameters;
	Get(rootIndex).InitAsConstants(constantCount, shaderRegister, 0u, visibility);
	return rootIndex;
}

uint32 RootSignature::AddConstantBufferView(uint32 shaderRegister, D3D12_SHADER_VISIBILITY visibility)
{
	uint32 rootIndex = m_NumParameters;
	Get(rootIndex).InitAsConstantBufferView(shaderRegister, 0u, visibility);
	return rootIndex;
}

uint32 RootSignature::AddRootSRV(uint32 shaderRegister, D3D12_SHADER_VISIBILITY visibility)
{
	uint32 rootIndex = m_NumParameters;
	Get(rootIndex).InitAsShaderResourceView(shaderRegister, 0u, visibility);
	return rootIndex;
}

uint32 RootSignature::AddRootUAV(uint32 shaderRegister, D3D12_SHADER_VISIBILITY visibility)
{
	uint32 rootIndex = m_NumParameters;
	Get(rootIndex).InitAsUnorderedAccessView(shaderRegister, 0u, visibility);
	return rootIndex;
}

uint32 RootSignature::AddDescriptorTable(uint32 rangeCount, D3D12_SHADER_VISIBILITY visibility)
{
	uint32 rootIndex = m_NumParameters;
	Get(rootIndex).InitAsDescriptorTable(rangeCount, m_DescriptorTableRanges[rootIndex].data(), visibility);
	return rootIndex;
}

uint32 RootSignature::AddDescriptorTableSimple(uint32 startRegisterSlot, D3D12_DESCRIPTOR_RANGE_TYPE type, uint32 count, D3D12_SHADER_VISIBILITY visibility)
{
	uint32 rootIndex = AddDescriptorTable(1, visibility);
	AddDescriptorTableRange(rootIndex, 0, startRegisterSlot, 0, type, count, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND);
	return rootIndex;
}

void RootSignature::AddDescriptorTableRange(uint32 rootIndex, uint32 rangeIndex, uint32 startRegisterSlot, uint32 space, D3D12_DESCRIPTOR_RANGE_TYPE type, uint32 count, uint32 offsetFromTableStart)
{
	GetRange(rootIndex, rangeIndex).Init(type, count, startRegisterSlot, space, offsetFromTableStart);
}

void RootSignature::AddStaticSampler(const D3D12_STATIC_SAMPLER_DESC& samplerDesc)
{
	m_StaticSamplers.push_back(CD3DX12_STATIC_SAMPLER_DESC(samplerDesc));
}

void RootSignature::Finalize(const char* pName, D3D12_ROOT_SIGNATURE_FLAGS flags, bool addDefaultStaticSamplers)
{
	D3D12_ROOT_SIGNATURE_FLAGS visibilityFlags =
		D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS
		| D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS
		| D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS
		| D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS;

	D3D12_FEATURE_DATA_D3D12_OPTIONS7 featureCaps{};
	GetParent()->GetDevice()->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &featureCaps, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS7));
	if (featureCaps.MeshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED)
	{
		visibilityFlags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS
			| D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS;
	}

	if (addDefaultStaticSamplers)
	{
		int staticSamplerRegisterSlot = 10;
		AddStaticSampler(CD3DX12_STATIC_SAMPLER_DESC(staticSamplerRegisterSlot++, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP));
		AddStaticSampler(CD3DX12_STATIC_SAMPLER_DESC(staticSamplerRegisterSlot++, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP));
		AddStaticSampler(CD3DX12_STATIC_SAMPLER_DESC(staticSamplerRegisterSlot++, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER));

		AddStaticSampler(CD3DX12_STATIC_SAMPLER_DESC(staticSamplerRegisterSlot++, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP));
		AddStaticSampler(CD3DX12_STATIC_SAMPLER_DESC(staticSamplerRegisterSlot++, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP));
		AddStaticSampler(CD3DX12_STATIC_SAMPLER_DESC(staticSamplerRegisterSlot++, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER));

		AddStaticSampler(CD3DX12_STATIC_SAMPLER_DESC(staticSamplerRegisterSlot++, D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP));
		AddStaticSampler(CD3DX12_STATIC_SAMPLER_DESC(staticSamplerRegisterSlot++, D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP));
		AddStaticSampler(CD3DX12_STATIC_SAMPLER_DESC(staticSamplerRegisterSlot++, D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER));

		AddStaticSampler(CD3DX12_STATIC_SAMPLER_DESC(staticSamplerRegisterSlot++, D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP));
		AddStaticSampler(CD3DX12_STATIC_SAMPLER_DESC(staticSamplerRegisterSlot++, D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 0.0f, 16u, D3D12_COMPARISON_FUNC_GREATER));
	}

	for (size_t i = 0; i < m_NumParameters; ++i)
	{
		D3D12_ROOT_PARAMETER& rootParameter = m_RootParameters[i];
		switch (rootParameter.ShaderVisibility)
		{
		case D3D12_SHADER_VISIBILITY_VERTEX:
			visibilityFlags = visibilityFlags & ~D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS;
			break;
		case D3D12_SHADER_VISIBILITY_GEOMETRY:
			visibilityFlags = visibilityFlags & ~D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
			break;
		case D3D12_SHADER_VISIBILITY_PIXEL:
			visibilityFlags = visibilityFlags & ~D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;
			break;
		case D3D12_SHADER_VISIBILITY_HULL:
			visibilityFlags = visibilityFlags & ~D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS;
			break;
		case D3D12_SHADER_VISIBILITY_DOMAIN:
			visibilityFlags = visibilityFlags & ~D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS;
			break;
		case D3D12_SHADER_VISIBILITY_MESH:
			visibilityFlags = visibilityFlags & ~D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS;
			break;
		case D3D12_SHADER_VISIBILITY_AMPLIFICATION:
			visibilityFlags = visibilityFlags & ~D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS;
			break;
		case D3D12_SHADER_VISIBILITY_ALL:
			visibilityFlags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
			break;
		default:
			noEntry();
			break;
		}
		if (rootParameter.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
		{
			//Fixup the table ranges because the rootsignature can be dynamically resized
			rootParameter.DescriptorTable.pDescriptorRanges = m_DescriptorTableRanges[i].data();
			switch (rootParameter.DescriptorTable.pDescriptorRanges->RangeType)
			{
			case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
			case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
			case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
				m_DescriptorTableMask.SetBit((uint32)i);
				break;
			case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER:
				m_SamplerMask.SetBit((uint32)i);
				break;
			default:
				noEntry();
				break;
			}

			for (uint32 j = 0; j < rootParameter.DescriptorTable.NumDescriptorRanges; ++j)
			{
				const D3D12_DESCRIPTOR_RANGE& range = rootParameter.DescriptorTable.pDescriptorRanges[j];
				m_DescriptorTableSizes[i] = range.NumDescriptors;
			}
		}
	}

	if (!EnumHasAnyFlags(flags, D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE))
	{
		flags |= visibilityFlags;
		flags |= D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
		flags |= D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;
	}

	constexpr uint32 recommendedDwords = 12;
	uint32 dwords = GetDWordSize();
	if (dwords > recommendedDwords)
	{
		E_LOG(Warning, "[RootSignature::Finalize] RootSignature '%s' uses %d DWORDs while under %d is recommended", pName, dwords, recommendedDwords);
	}

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC desc = {};
	desc.Init_1_0(m_NumParameters, m_RootParameters.data(), (uint32)m_StaticSamplers.size(), m_StaticSamplers.data(), flags);
	RefCountPtr<ID3DBlob> pDataBlob, pErrorBlob;
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

void RootSignature::FinalizeFromShader(const char* pName, const ShaderBase* pShader)
{
	RefCountPtr<ID3D12VersionedRootSignatureDeserializer> pDeserializer;
	VERIFY_HR_EX(D3D12CreateVersionedRootSignatureDeserializer(pShader->GetByteCode(), pShader->GetByteCodeSize(), IID_PPV_ARGS(pDeserializer.GetAddressOf())), GetParent()->GetDevice());
	const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* pDesc;
	pDeserializer->GetRootSignatureDescAtVersion(D3D_ROOT_SIGNATURE_VERSION_1_0, &pDesc);
	const D3D12_ROOT_SIGNATURE_DESC& rsDesc = pDesc->Desc_1_0;

	m_NumParameters = rsDesc.NumParameters;
	m_StaticSamplers.resize(rsDesc.NumStaticSamplers);

	memcpy(m_StaticSamplers.data(), rsDesc.pStaticSamplers, m_StaticSamplers.size() * sizeof(D3D12_STATIC_SAMPLER_DESC));
	memcpy(m_RootParameters.data(), rsDesc.pParameters, m_NumParameters * sizeof(D3D12_ROOT_PARAMETER));

	for (uint32 i = 0; i < rsDesc.NumParameters; ++i)
	{
		const D3D12_ROOT_PARAMETER& rootParameter = rsDesc.pParameters[i];
		if (rootParameter.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
		{
			m_DescriptorTableRanges[i].resize(rootParameter.DescriptorTable.NumDescriptorRanges);
			memcpy(m_DescriptorTableRanges[i].data(), rootParameter.DescriptorTable.pDescriptorRanges, rootParameter.DescriptorTable.NumDescriptorRanges * sizeof(D3D12_DESCRIPTOR_RANGE));
		}
	}

	m_BindlessViewsIndex = m_NumParameters - 2;
	m_BindlessSamplersIndex = m_NumParameters - 1;

	Finalize(pName, rsDesc.Flags, false);
}

uint32 RootSignature::GetDWordSize() const
{
	uint32 count = 0;
	for (size_t i = 0; i < m_NumParameters; ++i)
	{
		const D3D12_ROOT_PARAMETER& rootParameter = m_RootParameters[i];
		switch (rootParameter.ParameterType)
		{
		case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
			count += rootParameter.Constants.Num32BitValues;
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
