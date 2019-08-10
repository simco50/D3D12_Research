#include "stdafx.h"
#include "RootSignature.h"

RootSignature::RootSignature() 
	: m_NumParameters(0)
{
}

void RootSignature::SetSize(uint32 size, bool shrink /*= true*/)
{
	if (size != m_NumParameters && (shrink || size > m_NumParameters))
	{
		assert(size <= MAX_NUM_DESCRIPTORS);
		m_RootParameters.resize(size);
		m_DescriptorTableSizes.resize(size);
		m_DescriptorTableRanges.resize(size);
		m_NumParameters = size;
	}
}

void RootSignature::SetRootConstants(uint32 rootIndex, uint32 shaderRegister, uint32 constantCount, D3D12_SHADER_VISIBILITY visibility)
{
	SetSize(rootIndex + 1);
	D3D12_ROOT_PARAMETER1 & data = m_RootParameters[rootIndex];
	data.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	data.Constants.Num32BitValues = constantCount;
	data.Constants.RegisterSpace = 0;
	data.Constants.ShaderRegister = shaderRegister;
	data.ShaderVisibility = visibility;
}

void RootSignature::SetConstantBufferView(uint32 rootIndex, uint32 shaderRegister, D3D12_SHADER_VISIBILITY visibility)
{
	SetSize(rootIndex + 1);
	D3D12_ROOT_PARAMETER1 & data = m_RootParameters[rootIndex];
	data.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	data.Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;
	data.Descriptor.RegisterSpace = 0;
	data.Descriptor.ShaderRegister = shaderRegister;
	data.ShaderVisibility = visibility;
}

void RootSignature::SetShaderResourceView(uint32 rootIndex, uint32 shaderRegister, D3D12_SHADER_VISIBILITY visibility)
{
	SetSize(rootIndex + 1);
	D3D12_ROOT_PARAMETER1 & data = m_RootParameters[rootIndex];
	data.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	data.Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;
	data.Descriptor.RegisterSpace = 0;
	data.Descriptor.ShaderRegister = shaderRegister;
	data.ShaderVisibility = visibility;
}

void RootSignature::SetUnorderedAccessView(uint32 rootIndex, uint32 shaderRegister, D3D12_SHADER_VISIBILITY visibility)
{
	SetSize(rootIndex + 1);
	D3D12_ROOT_PARAMETER1 & data = m_RootParameters[rootIndex];
	data.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
	data.Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;
	data.Descriptor.RegisterSpace = 0;
	data.Descriptor.ShaderRegister = shaderRegister;
	data.ShaderVisibility = visibility;
}

void RootSignature::SetDescriptorTable(uint32 rootIndex, uint32 rangeCount, D3D12_SHADER_VISIBILITY visibility)
{
	SetSize(rootIndex + 1);
	D3D12_ROOT_PARAMETER1& data = m_RootParameters[rootIndex];
	data.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	data.ShaderVisibility = visibility;
	data.DescriptorTable.NumDescriptorRanges = rangeCount;
	data.DescriptorTable.pDescriptorRanges = m_DescriptorTableRanges[rootIndex].data();
}

void RootSignature::SetDescriptorTableRange(uint32 rootIndex, uint32 rangeIndex, uint32 startRegisterSlot, D3D12_DESCRIPTOR_RANGE_TYPE type, uint32 count)
{
	assert(rangeIndex < MAX_RANGES_PER_TABLE);
	D3D12_DESCRIPTOR_RANGE1& range = m_DescriptorTableRanges[rootIndex][rangeIndex];
	range.RangeType = type;
	range.NumDescriptors = count;
	range.BaseShaderRegister = startRegisterSlot;
	range.RegisterSpace = 0;
	range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
	range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
}

void RootSignature::SetDescriptorTableSimple(uint32 rootIndex, uint32 startRegisterSlot, D3D12_DESCRIPTOR_RANGE_TYPE type, uint32 count, D3D12_SHADER_VISIBILITY visibility)
{
	SetDescriptorTable(rootIndex, 1, visibility);
	SetDescriptorTableRange(rootIndex, 0, startRegisterSlot, type, count);
}

void RootSignature::AddStaticSampler(uint32 shaderRegister, D3D12_SAMPLER_DESC samplerDesc, D3D12_SHADER_VISIBILITY visibility)
{
	D3D12_STATIC_SAMPLER_DESC desc = {};
	desc.Filter = samplerDesc.Filter;
	desc.AddressU = samplerDesc.AddressU;
	desc.AddressV = samplerDesc.AddressV;
	desc.AddressW = samplerDesc.AddressW;
	desc.MipLODBias = samplerDesc.MipLODBias;
	desc.MaxAnisotropy = samplerDesc.MaxAnisotropy;
	desc.ComparisonFunc = samplerDesc.ComparisonFunc;
	desc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
	desc.MinLOD = samplerDesc.MinLOD;
	desc.MaxLOD = samplerDesc.MaxLOD;
	desc.ShaderRegister = shaderRegister;
	desc.RegisterSpace = 0;
	desc.ShaderVisibility = visibility;

	if (desc.AddressU == D3D12_TEXTURE_ADDRESS_MODE_BORDER ||
		desc.AddressV == D3D12_TEXTURE_ADDRESS_MODE_BORDER ||
		desc.AddressW == D3D12_TEXTURE_ADDRESS_MODE_BORDER)
	{
		desc.BorderColor = samplerDesc.BorderColor[3] * samplerDesc.BorderColor[0] == 1.0f == 0 ? D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK : D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
	}
	m_StaticSamplers.push_back(desc);
}

void RootSignature::Finalize(const char* pName, ID3D12Device* pDevice, D3D12_ROOT_SIGNATURE_FLAGS flags)
{
	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC desc = {};

	for (size_t i = 0; i < m_RootParameters.size(); ++i)
	{
		D3D12_ROOT_PARAMETER1& rootParameter = m_RootParameters[i];
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
				assert(false);
				break;
			}

			for (uint32 j = 0; j < rootParameter.DescriptorTable.NumDescriptorRanges; ++j)
			{
				m_DescriptorTableSizes[i] = rootParameter.DescriptorTable.pDescriptorRanges[j].NumDescriptors;
			}
		}
	}

	constexpr uint32 recommendedDwords = 12;
	uint32 dwords = GetDWordSize();
	if (dwords > recommendedDwords)
	{
		E_LOG(Warning, "[RootSignature::Finalize] RootSignature '%s' uses %d DWORDs while under %d is recommended", pName, dwords, recommendedDwords);
	}

	desc.Init_1_1(m_NumParameters, m_RootParameters.data(), (uint32)m_StaticSamplers.size(), m_StaticSamplers.data(), flags);

	ComPtr<ID3DBlob> pDataBlob, pErrorBlob;
	HR(D3D12SerializeVersionedRootSignature(&desc, pDataBlob.GetAddressOf(), pErrorBlob.GetAddressOf()));
	HR(pDevice->CreateRootSignature(0, pDataBlob->GetBufferPointer(), pDataBlob->GetBufferSize(), IID_PPV_ARGS(m_pRootSignature.GetAddressOf())));
	SetD3DObjectName(m_pRootSignature.Get(), pName);
}

uint32 RootSignature::GetDWordSize() const
{
	uint32 count = 0;
	for (size_t i = 0; i < m_RootParameters.size(); ++i)
	{
		const D3D12_ROOT_PARAMETER1& rootParameter = m_RootParameters[i];
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
