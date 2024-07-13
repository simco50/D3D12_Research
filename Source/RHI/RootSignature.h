#pragma once
#include "DeviceResource.h"

/*
	The RootSignature describes how the GPU resources map to the shader.
	A Shader Resource can get bound to a root index directly or a descriptor table.
	A root index maps to a shader register (eg. b0, t4, u2, ...)
*/

class RootSignature : public DeviceObject
{
public:
	static constexpr int sMaxNumParameters = 8;

	RootSignature(GraphicsDevice* pParent);

	template<typename T>
	void AddRootConstants(uint32 shaderRegister, uint32 space, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL)
	{
		AddRootConstants(shaderRegister, sizeof(T) / sizeof(uint32), space, visibility);
	}
	void AddRootConstants(uint32 shaderRegister, uint32 constantCount, uint32 space, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);
	void AddRootCBV(uint32 shaderRegister, uint32 space, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);
	void AddRootSRV(uint32 shaderRegister, uint32 space, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);
	void AddRootUAV(uint32 shaderRegister, uint32 space, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);
	void AddDescriptorTable(uint32 shaderRegister, uint32 numDescriptors, D3D12_DESCRIPTOR_RANGE_TYPE type, uint32 space, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);

	void AddStaticSampler(uint32 registerSlot, D3D12_FILTER filter, D3D12_TEXTURE_ADDRESS_MODE wrapMode, D3D12_COMPARISON_FUNC compareFunc = D3D12_COMPARISON_FUNC_ALWAYS);

	void Finalize(const char* pName, D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_NONE);

	ID3D12RootSignature* GetRootSignature() const { return m_pRootSignature.Get(); }

	uint32 GetNumRootConstants(uint32 rootIndex) const { gAssert(IsRootConstant(rootIndex)); return m_RootParameters[rootIndex].Data.Constants.Num32BitValues; }
	bool IsRootConstant(uint32 rootIndex) const { return m_RootParameters[rootIndex].Data.ParameterType == D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; }
	uint32 GetDescriptorTableSize(uint32 rootIndex) const;
	uint32 GetNumRootParameters() const { return m_NumParameters; }

	uint32 GetDWORDSize() const;
private:
	struct RootParameter
	{
		CD3DX12_ROOT_PARAMETER1 Data;
		CD3DX12_DESCRIPTOR_RANGE1 Range;
	};
	StaticArray<RootParameter, sMaxNumParameters> m_RootParameters{};
	Array<D3D12_STATIC_SAMPLER_DESC> m_StaticSamplers;
	Ref<ID3D12RootSignature> m_pRootSignature;
	uint32 m_NumParameters;
};
