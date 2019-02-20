#pragma once

struct RootParameter
{
	void AsConstantBufferView(uint32 registersSlot, uint32 registerState, D3D12_SHADER_VISIBILITY visibility)
	{
		Data.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		Data.Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE;
		Data.Descriptor.RegisterSpace = registerState;
		Data.Descriptor.ShaderRegister = registersSlot;
		Data.ShaderVisibility = visibility;
	}

	void AsShaderResourceView(uint32 registersSlot, uint32 registerState, D3D12_SHADER_VISIBILITY visibility)
	{
		Data.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
		Data.Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE;
		Data.Descriptor.RegisterSpace = registerState;
		Data.Descriptor.ShaderRegister = registersSlot;
		Data.ShaderVisibility = visibility;
	}

	void AsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE Type, UINT Register, UINT Count, D3D12_SHADER_VISIBILITY Visibility = D3D12_SHADER_VISIBILITY_ALL)
	{
		AsDescriptorTable(1, Visibility);
		SetTableRange(0, Type, Register, Count);
	}

	void AsDescriptorTable(UINT RangeCount, D3D12_SHADER_VISIBILITY Visibility = D3D12_SHADER_VISIBILITY_ALL)
	{
		Data.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		Data.ShaderVisibility = Visibility;
		Data.DescriptorTable.NumDescriptorRanges = RangeCount;
		Data.DescriptorTable.pDescriptorRanges = m_DescriptorTableRanges;
	}

	void SetTableRange(UINT RangeIndex, D3D12_DESCRIPTOR_RANGE_TYPE Type, UINT Register, UINT Count, UINT Space = 0)
	{
		D3D12_DESCRIPTOR_RANGE1& range = m_DescriptorTableRanges[RangeIndex];
		range.RangeType = Type;
		range.NumDescriptors = Count;
		range.BaseShaderRegister = Register;
		range.RegisterSpace = Space;
		range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
		range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
	}

	D3D12_ROOT_PARAMETER1 Data = {};
	D3D12_DESCRIPTOR_RANGE1 m_DescriptorTableRanges[4];
};

class RootSignature
{
public:
	RootSignature(uint32 numRootParameters)
	{
		m_NumParameters = numRootParameters;
		m_RootParameters.resize(numRootParameters);
		m_DescriptorTableSizes.resize(numRootParameters);
	}

	void AddStaticSampler(uint32 slot, D3D12_SAMPLER_DESC samplerDesc, D3D12_SHADER_VISIBILITY visibility);

	RootParameter& operator[](uint32 i) { return m_RootParameters[i]; }
	const RootParameter& operator[](uint32 i) const { return m_RootParameters[i]; }

	void Finalize(ID3D12Device* pDevice, D3D12_ROOT_SIGNATURE_FLAGS flags);

	ID3D12RootSignature* GetRootSignature() const { return m_pRootSignature.Get(); }

private:
	std::vector<RootParameter> m_RootParameters;
	std::vector<uint32> m_DescriptorTableSizes;
	std::vector<D3D12_STATIC_SAMPLER_DESC> m_StaticSamplers;
	ComPtr<ID3D12RootSignature> m_pRootSignature;

	BitField<16, uint32> m_DescriptorTableMask;
	BitField<16, uint32> m_SamplerMask;
	uint32 m_NumParameters = 0;
};

