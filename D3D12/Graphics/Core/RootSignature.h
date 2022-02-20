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

using RootSignatureMask = BitField16;
static constexpr int MAX_NUM_ROOT_PARAMETERS = RootSignatureMask::Size();

class RootSignature : public GraphicsObject
{
public:
	RootSignature(GraphicsDevice* pParent);

	template<typename T>
	uint32 AddRootConstants(uint32 shaderRegister, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL)
	{
		return AddRootConstants(shaderRegister, sizeof(T) / sizeof(uint32), visibility);
	}
	uint32 AddRootConstants(uint32 shaderRegister, uint32 constantCount, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);
	uint32 AddConstantBufferView(uint32 shaderRegister, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);
	uint32 AddRootSRV(uint32 shaderRegister, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);
	uint32 AddRootUAV(uint32 shaderRegister, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);
	uint32 AddDescriptorTable(uint32 rangeCount, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);
	uint32 AddDescriptorTableSimple(uint32 startRegisterSlot, D3D12_DESCRIPTOR_RANGE_TYPE type, uint32 count, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);
	void AddDescriptorTableRange(uint32 rootIndex, uint32 rangeIndex, uint32 startRegisterSlot, uint32 space, D3D12_DESCRIPTOR_RANGE_TYPE type, uint32 count, uint32 heapSlotOffset);

	void AddStaticSampler(const D3D12_STATIC_SAMPLER_DESC& samplerDesc);

	void Finalize(const char* pName, D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_NONE, bool addDefaultStaticSamplers = true);
	void FinalizeFromShader(const char* pName, const ShaderBase* pShader);

	ID3D12RootSignature* GetRootSignature() const { return m_pRootSignature.Get(); }

	const RootSignatureMask& GetSamplerTableMask() const { return m_SamplerMask; }
	const RootSignatureMask& GetDescriptorTableMask() const { return m_DescriptorTableMask; }
	const std::array<uint32, MAX_NUM_ROOT_PARAMETERS>& GetDescriptorTableSizes() const { return m_DescriptorTableSizes; }

	uint32 GetDWordSize() const;
	uint32 GetBindlessViewIndex() const { return m_BindlessViewsIndex; }
	uint32 GetBindlessSamplerIndex() const { return m_BindlessSamplersIndex; }

private:

	CD3DX12_ROOT_PARAMETER& Get(uint32 index)
	{
		check(index < MAX_NUM_ROOT_PARAMETERS);
		m_NumParameters = Math::Max(index + 1, m_NumParameters);
		return m_RootParameters[index];
	}

	CD3DX12_DESCRIPTOR_RANGE& GetRange(uint32 rootIndex, uint32 rangeIndex)
	{
		check(rootIndex < MAX_NUM_ROOT_PARAMETERS);
		m_DescriptorTableRanges[rootIndex].resize(Math::Max<uint32>((uint32)m_DescriptorTableRanges[rootIndex].size(), rangeIndex + 1));
		return m_DescriptorTableRanges[rootIndex][rangeIndex];
	}

	std::array<CD3DX12_ROOT_PARAMETER, MAX_NUM_ROOT_PARAMETERS> m_RootParameters{};
	std::array<uint32, MAX_NUM_ROOT_PARAMETERS> m_DescriptorTableSizes{};
	std::vector<CD3DX12_STATIC_SAMPLER_DESC> m_StaticSamplers;
	std::array<std::vector<CD3DX12_DESCRIPTOR_RANGE>, MAX_NUM_ROOT_PARAMETERS> m_DescriptorTableRanges{};
	ComPtr<ID3D12RootSignature> m_pRootSignature;

	RootSignatureMask m_DescriptorTableMask;
	RootSignatureMask m_SamplerMask;
	uint32 m_NumParameters;
	uint32 m_BindlessViewsIndex;
	uint32 m_BindlessSamplersIndex;
};
