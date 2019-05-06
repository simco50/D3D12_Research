#pragma once

class RootSignature
{
public:
	static const int MAX_NUM_DESCRIPTORS = 16;
	static const int MAX_RANGES_PER_TABLE = 2;
	using RootSignatureDescriptorMask = BitField32;
	static_assert(MAX_NUM_DESCRIPTORS <= BitField32::Capacity(), "Descriptor bitfield is not large enough");

	RootSignature(uint32 numRootParameters);

	void SetRootConstants(uint32 rootIndex, uint32 registerSlot, uint32 constantCount, D3D12_SHADER_VISIBILITY visibility);
	void SetConstantBufferView(uint32 rootIndex, uint32 registersSlot, D3D12_SHADER_VISIBILITY visibility);
	void SetShaderResourceView(uint32 rootIndex, uint32 registersSlot, D3D12_SHADER_VISIBILITY visibility);
	void SetUnorderedAccessView(uint32 rootIndex, uint32 registersSlot, D3D12_SHADER_VISIBILITY visibility);
	void SetDescriptorTable(uint32 rootIndex, uint32 rangeCount, D3D12_SHADER_VISIBILITY visibility);
	void SetDescriptorTableRange(uint32 rootIndex, uint32 rangeIndex, uint32 startRegisterSlot, D3D12_DESCRIPTOR_RANGE_TYPE type, uint32 count);
	void SetDescriptorTableSimple(uint32 rootIndex, uint32 startRegisterSlot, D3D12_DESCRIPTOR_RANGE_TYPE type, uint32 count, D3D12_SHADER_VISIBILITY visibility);

	void AddStaticSampler(uint32 slot, D3D12_SAMPLER_DESC samplerDesc, D3D12_SHADER_VISIBILITY visibility);

	void Finalize(ID3D12Device* pDevice, D3D12_ROOT_SIGNATURE_FLAGS flags);

	ID3D12RootSignature* GetRootSignature() const { return m_pRootSignature.Get(); }

	const RootSignatureDescriptorMask& GetSamplerTableMask() const { return m_SamplerMask; }
	const RootSignatureDescriptorMask& GetDescriptorTableMask() const { return m_DescriptorTableMask; }
	const std::vector<uint32>& GetDescriptorTableSizes() const { return m_DescriptorTableSizes; }

private:
	std::vector<D3D12_ROOT_PARAMETER1> m_RootParameters;
	std::vector<uint32> m_DescriptorTableSizes;
	std::vector<D3D12_STATIC_SAMPLER_DESC> m_StaticSamplers;
	std::vector<std::array<D3D12_DESCRIPTOR_RANGE1, MAX_RANGES_PER_TABLE>> m_DescriptorTableRanges;
	ComPtr<ID3D12RootSignature> m_pRootSignature;

	RootSignatureDescriptorMask m_DescriptorTableMask;
	RootSignatureDescriptorMask m_SamplerMask;
	uint32 m_NumParameters;
};