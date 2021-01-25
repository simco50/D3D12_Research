#pragma once
#include "Core/BitField.h"
#include "GraphicsResource.h"

/*
	The RootSignature describes how the GPU resources map to the shader.
	A Shader Resource can get bound to a root index directly or a descriptor table.
	A root index maps to a shader register (eg. b0, t4, u2, ...)
	We keep a bitmask to later dynamically copy CPU descriptors to the GPU when rendering
*/

class ShaderBase;

class RootSignature : public GraphicsObject
{
public:
	static const int MAX_NUM_DESCRIPTORS = 16;
	static const int MAX_RANGES_PER_TABLE = 2;
	static_assert(MAX_NUM_DESCRIPTORS <= BitField32::Capacity(), "Descriptor bitfield is not large enough");

	RootSignature(Graphics* pParent);

	void SetSize(uint32 size, bool shrink = true);
	template<typename T>
	void SetRootConstants(uint32 rootIndex, uint32 shaderRegister, D3D12_SHADER_VISIBILITY visibility)
	{
		SetRootConstants(rootIndex, shaderRegister, sizeof(T) / sizeof(uint32), visibility);
	}
	void SetRootConstants(uint32 rootIndex, uint32 shaderRegister, uint32 constantCount, D3D12_SHADER_VISIBILITY visibility);
	void SetConstantBufferView(uint32 rootIndex, uint32 shaderRegister, D3D12_SHADER_VISIBILITY visibility);
	void SetShaderResourceView(uint32 rootIndex, uint32 shaderRegister, D3D12_SHADER_VISIBILITY visibility);
	void SetUnorderedAccessView(uint32 rootIndex, uint32 shaderRegister, D3D12_SHADER_VISIBILITY visibility);
	void SetDescriptorTable(uint32 rootIndex, uint32 rangeCount, D3D12_SHADER_VISIBILITY visibility);
	void SetDescriptorTableRange(uint32 rootIndex, uint32 rangeIndex, uint32 startRegisterSlot, D3D12_DESCRIPTOR_RANGE_TYPE type, uint32 count, uint32 heapSlotOffset);
	void SetDescriptorTableSimple(uint32 rootIndex, uint32 startRegisterSlot, D3D12_DESCRIPTOR_RANGE_TYPE type, uint32 count, D3D12_SHADER_VISIBILITY visibility);

	void AddStaticSampler(uint32 shaderRegister, const D3D12_STATIC_SAMPLER_DESC& samplerDesc, D3D12_SHADER_VISIBILITY visibility);

	void Finalize(const char* pName, D3D12_ROOT_SIGNATURE_FLAGS flags);
	void FinalizeFromShader(const char* pName, const ShaderBase* pShader);

	ID3D12RootSignature* GetRootSignature() const { return m_pRootSignature.Get(); }

	const BitField32& GetSamplerTableMask() const { return m_SamplerMask; }
	const BitField32& GetDescriptorTableMask() const { return m_DescriptorTableMask; }
	const std::vector<uint32>& GetDescriptorTableSizes() const { return m_DescriptorTableSizes; }

	uint32 GetDWordSize() const;

private:
	std::vector<D3D12_ROOT_PARAMETER> m_RootParameters;
	std::vector<uint32> m_DescriptorTableSizes;
	std::vector<D3D12_STATIC_SAMPLER_DESC> m_StaticSamplers;
	std::vector<std::array<D3D12_DESCRIPTOR_RANGE, MAX_RANGES_PER_TABLE>> m_DescriptorTableRanges;
	ComPtr<ID3D12RootSignature> m_pRootSignature;

	BitField32 m_DescriptorTableMask;
	BitField32 m_SamplerMask;
	uint32 m_NumParameters;
};
