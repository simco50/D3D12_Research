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
	D3D12_ROOT_PARAMETER1 Data = {};
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

