#include "stdafx.h"
#include "RootSignature.h"
#include "Shader.h"

RootSignature::RootSignature() 
	: m_NumParameters(0)
{
}

void RootSignature::SetSize(uint32 size, bool shrink /*= true*/)
{
	if (size != m_NumParameters && (shrink || size > m_NumParameters))
	{
		check(size <= MAX_NUM_DESCRIPTORS);
		m_RootParameters.resize(size);
		m_DescriptorTableSizes.resize(size);
		m_DescriptorTableRanges.resize(size);
		m_NumParameters = size;
	}
}

void RootSignature::SetRootConstants(uint32 rootIndex, uint32 shaderRegister, uint32 constantCount, D3D12_SHADER_VISIBILITY visibility)
{
	SetSize(rootIndex + 1);
	D3D12_ROOT_PARAMETER& data = m_RootParameters[rootIndex];
	data.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	data.Constants.Num32BitValues = constantCount;
	data.Constants.RegisterSpace = 0;
	data.Constants.ShaderRegister = shaderRegister;
	data.ShaderVisibility = visibility;
}

void RootSignature::SetConstantBufferView(uint32 rootIndex, uint32 shaderRegister, D3D12_SHADER_VISIBILITY visibility)
{
	SetSize(rootIndex + 1);
	D3D12_ROOT_PARAMETER& data = m_RootParameters[rootIndex];
	data.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	data.Descriptor.RegisterSpace = 0;
	data.Descriptor.ShaderRegister = shaderRegister;
	data.ShaderVisibility = visibility;
}

void RootSignature::SetShaderResourceView(uint32 rootIndex, uint32 shaderRegister, D3D12_SHADER_VISIBILITY visibility)
{
	SetSize(rootIndex + 1);
	D3D12_ROOT_PARAMETER& data = m_RootParameters[rootIndex];
	data.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	data.Descriptor.RegisterSpace = 0;
	data.Descriptor.ShaderRegister = shaderRegister;
	data.ShaderVisibility = visibility;
}

void RootSignature::SetUnorderedAccessView(uint32 rootIndex, uint32 shaderRegister, D3D12_SHADER_VISIBILITY visibility)
{
	SetSize(rootIndex + 1);
	D3D12_ROOT_PARAMETER& data = m_RootParameters[rootIndex];
	data.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
	data.Descriptor.RegisterSpace = 0;
	data.Descriptor.ShaderRegister = shaderRegister;
	data.ShaderVisibility = visibility;
}

void RootSignature::SetDescriptorTable(uint32 rootIndex, uint32 rangeCount, D3D12_SHADER_VISIBILITY visibility)
{
	SetSize(rootIndex + 1);
	D3D12_ROOT_PARAMETER& data = m_RootParameters[rootIndex];
	data.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	data.ShaderVisibility = visibility;
	data.DescriptorTable.NumDescriptorRanges = rangeCount;
	data.DescriptorTable.pDescriptorRanges = m_DescriptorTableRanges[rootIndex].data();
}

void RootSignature::SetDescriptorTableRange(uint32 rootIndex, uint32 rangeIndex, uint32 startRegisterSlot, D3D12_DESCRIPTOR_RANGE_TYPE type, uint32 count, uint32 heapSlotOffset)
{
	check(rangeIndex < MAX_RANGES_PER_TABLE);
	D3D12_DESCRIPTOR_RANGE& range = m_DescriptorTableRanges[rootIndex][rangeIndex];
	range.RangeType = type;
	range.NumDescriptors = count;
	range.BaseShaderRegister = startRegisterSlot;
	range.RegisterSpace = 0;
	range.OffsetInDescriptorsFromTableStart = heapSlotOffset;
}

void RootSignature::SetDescriptorTableSimple(uint32 rootIndex, uint32 startRegisterSlot, D3D12_DESCRIPTOR_RANGE_TYPE type, uint32 count, D3D12_SHADER_VISIBILITY visibility)
{
	SetDescriptorTable(rootIndex, 1, visibility);
	SetDescriptorTableRange(rootIndex, 0, startRegisterSlot, type, count, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND);
}

void RootSignature::AddStaticSampler(uint32 shaderRegister, const D3D12_STATIC_SAMPLER_DESC& samplerDesc, D3D12_SHADER_VISIBILITY visibility)
{
	m_StaticSamplers.push_back(samplerDesc);
}

void RootSignature::Finalize(const char* pName, ID3D12Device* pDevice, D3D12_ROOT_SIGNATURE_FLAGS flags)
{
	D3D12_ROOT_SIGNATURE_FLAGS visibilityFlags =
		D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS
		| D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS
		| D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS
		| D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS;

	D3D12_FEATURE_DATA_D3D12_OPTIONS7 featureCaps{};
	pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &featureCaps, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS7));
	if (featureCaps.MeshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED)
	{
		visibilityFlags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS
			| D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS;
	}

	for (size_t i = 0; i < m_RootParameters.size(); ++i)
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
				m_DescriptorTableSizes[i] = rootParameter.DescriptorTable.pDescriptorRanges[j].NumDescriptors;
				checkf(m_DescriptorTableSizes[i] != (uint32)~0, "Unbounded descriptors not supported. Just use a large number");
			}
		}
	}

	if ((flags & D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE) == 0)
	{
		flags |= visibilityFlags;
	}

	constexpr uint32 recommendedDwords = 12;
	uint32 dwords = GetDWordSize();
	if (dwords > recommendedDwords)
	{
		E_LOG(Warning, "[RootSignature::Finalize] RootSignature '%s' uses %d DWORDs while under %d is recommended", pName, dwords, recommendedDwords);
	}

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC desc = {};
	desc.Init_1_0(m_NumParameters, m_RootParameters.data(), (uint32)m_StaticSamplers.size(), m_StaticSamplers.data(), flags);
	ComPtr<ID3DBlob> pDataBlob, pErrorBlob;
	D3D12SerializeVersionedRootSignature(&desc, pDataBlob.GetAddressOf(), pErrorBlob.GetAddressOf());
	if (pErrorBlob)
	{
		const char* pError = (char*)pErrorBlob->GetBufferPointer();
		E_LOG(Error, "RootSignature serialization error: %s", pError);
		return;
	}
	VERIFY_HR_EX(pDevice->CreateRootSignature(0, pDataBlob->GetBufferPointer(), pDataBlob->GetBufferSize(), IID_PPV_ARGS(m_pRootSignature.GetAddressOf())), pDevice);
	D3D::SetObjectName(m_pRootSignature.Get(), pName);
}

void RootSignature::FinalizeFromShader(const char* pName, const Shader& shader, ID3D12Device* pDevice)
{
	ComPtr<ID3D12VersionedRootSignatureDeserializer> pDeserializer;
	VERIFY_HR_EX(D3D12CreateVersionedRootSignatureDeserializer(shader.GetByteCode(), shader.GetByteCodeSize(), IID_PPV_ARGS(pDeserializer.GetAddressOf())), pDevice);
	const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* pDesc = pDeserializer->GetUnconvertedRootSignatureDesc();

	m_NumParameters = pDesc->Desc_1_0.NumParameters;
	m_DescriptorTableRanges.resize(m_NumParameters);
	m_DescriptorTableSizes.resize(m_NumParameters);
	m_RootParameters.resize(m_NumParameters);
	m_StaticSamplers.resize(pDesc->Desc_1_0.NumStaticSamplers);

	memcpy(m_StaticSamplers.data(), pDesc->Desc_1_0.pStaticSamplers, m_StaticSamplers.size() * sizeof(D3D12_STATIC_SAMPLER_DESC));
	memcpy(m_RootParameters.data(), pDesc->Desc_1_0.pParameters, m_RootParameters.size() * sizeof(D3D12_ROOT_PARAMETER));

	for (uint32 i = 0; i < pDesc->Desc_1_0.NumParameters; ++i)
	{
		const D3D12_ROOT_PARAMETER& rootParameter = pDesc->Desc_1_0.pParameters[i];
		if (rootParameter.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
		{
			memcpy(m_DescriptorTableRanges[i].data(), rootParameter.DescriptorTable.pDescriptorRanges, rootParameter.DescriptorTable.NumDescriptorRanges * sizeof(D3D12_DESCRIPTOR_RANGE));
		}
	}

	Finalize(pName, pDevice, pDesc->Desc_1_0.Flags);
}

uint32 RootSignature::GetDWordSize() const
{
	uint32 count = 0;
	for (size_t i = 0; i < m_RootParameters.size(); ++i)
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