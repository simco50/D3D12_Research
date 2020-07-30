//*********************************************************
//
// Copyright (c) Simon Coenen. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#ifndef __D3DX12_EXTRA_H__
#define __D3DX12_EXTRA_H__

#include "d3d12.h"

#if defined( __cplusplus )


struct CD3DX12_INPUT_ELEMENT_DESC : public D3D12_INPUT_ELEMENT_DESC
{
	CD3DX12_INPUT_ELEMENT_DESC() = default;
	explicit CD3DX12_INPUT_ELEMENT_DESC(const D3D12_INPUT_ELEMENT_DESC& o) noexcept :
		D3D12_INPUT_ELEMENT_DESC(o)
	{}
	CD3DX12_INPUT_ELEMENT_DESC(
		const char* semanticName, 
		DXGI_FORMAT format, 
		uint32 semanticIndex = 0, 
		uint32 byteOffset = D3D12_APPEND_ALIGNED_ELEMENT, 
		uint32 inputSlot = 0, 
		D3D12_INPUT_CLASSIFICATION inputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 
		uint32 instanceDataStepRate = 0) noexcept
	{
		SemanticName = semanticName;
		SemanticIndex = semanticIndex;
		Format = format;
		InputSlot = inputSlot;
		AlignedByteOffset = byteOffset;
		InputSlotClass = inputSlotClass;
		InstanceDataStepRate = instanceDataStepRate;
	}
};

struct CD3DX12_QUERY_HEAP_DESC : public D3D12_QUERY_HEAP_DESC
{
	CD3DX12_QUERY_HEAP_DESC() = default;
	CD3DX12_QUERY_HEAP_DESC(uint32 count,
		D3D12_QUERY_HEAP_TYPE type,
		uint32 nodeMask = 0)
	{
		Type = type;
		Count = count;
		NodeMask = nodeMask;
	}
};

class CD3DX12_PIPELINE_STATE_STREAM_HELPER
{
private:
	template<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE T> struct CD3DX12_PIPELINE_STATE_SUBOJECT_TYPE_TRAITS { static_assert(sizeof(T) == -1, "Type traits for this subobject does not exist. Add one in this file."); };
	template<> struct CD3DX12_PIPELINE_STATE_SUBOJECT_TYPE_TRAITS<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS> { using Type = D3D12_PIPELINE_STATE_FLAGS; };
	template<> struct CD3DX12_PIPELINE_STATE_SUBOJECT_TYPE_TRAITS<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK> { using Type = UINT; };
	template<> struct CD3DX12_PIPELINE_STATE_SUBOJECT_TYPE_TRAITS<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE> { using Type = ID3D12RootSignature*; };
	template<> struct CD3DX12_PIPELINE_STATE_SUBOJECT_TYPE_TRAITS<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT> { using Type = D3D12_INPUT_LAYOUT_DESC; };
	template<> struct CD3DX12_PIPELINE_STATE_SUBOJECT_TYPE_TRAITS<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE> { using Type = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE; };
	template<> struct CD3DX12_PIPELINE_STATE_SUBOJECT_TYPE_TRAITS<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY> { using Type = D3D12_PRIMITIVE_TOPOLOGY_TYPE; };
	template<> struct CD3DX12_PIPELINE_STATE_SUBOJECT_TYPE_TRAITS<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS> { using Type = D3D12_SHADER_BYTECODE; };
	template<> struct CD3DX12_PIPELINE_STATE_SUBOJECT_TYPE_TRAITS<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS> { using Type = D3D12_SHADER_BYTECODE; };
	template<> struct CD3DX12_PIPELINE_STATE_SUBOJECT_TYPE_TRAITS<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT> { using Type = D3D12_STREAM_OUTPUT_DESC; };
	template<> struct CD3DX12_PIPELINE_STATE_SUBOJECT_TYPE_TRAITS<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS> { using Type = D3D12_SHADER_BYTECODE; };
	template<> struct CD3DX12_PIPELINE_STATE_SUBOJECT_TYPE_TRAITS<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS> { using Type = D3D12_SHADER_BYTECODE; };
	template<> struct CD3DX12_PIPELINE_STATE_SUBOJECT_TYPE_TRAITS<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS> { using Type = D3D12_SHADER_BYTECODE; };
	template<> struct CD3DX12_PIPELINE_STATE_SUBOJECT_TYPE_TRAITS<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS> { using Type = D3D12_SHADER_BYTECODE; };
	template<> struct CD3DX12_PIPELINE_STATE_SUBOJECT_TYPE_TRAITS<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND> { using Type = CD3DX12_BLEND_DESC; };
	template<> struct CD3DX12_PIPELINE_STATE_SUBOJECT_TYPE_TRAITS<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL> { using Type = CD3DX12_DEPTH_STENCIL_DESC; };
	template<> struct CD3DX12_PIPELINE_STATE_SUBOJECT_TYPE_TRAITS<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1> { using Type = CD3DX12_DEPTH_STENCIL_DESC1; };
	template<> struct CD3DX12_PIPELINE_STATE_SUBOJECT_TYPE_TRAITS<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT> { using Type = DXGI_FORMAT; };
	template<> struct CD3DX12_PIPELINE_STATE_SUBOJECT_TYPE_TRAITS<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER> { using Type = CD3DX12_RASTERIZER_DESC; };
	template<> struct CD3DX12_PIPELINE_STATE_SUBOJECT_TYPE_TRAITS<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS> { using Type = D3D12_RT_FORMAT_ARRAY; };
	template<> struct CD3DX12_PIPELINE_STATE_SUBOJECT_TYPE_TRAITS<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC> { using Type = DXGI_SAMPLE_DESC; };
	template<> struct CD3DX12_PIPELINE_STATE_SUBOJECT_TYPE_TRAITS<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK> { using Type = UINT; };
	template<> struct CD3DX12_PIPELINE_STATE_SUBOJECT_TYPE_TRAITS<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO> { using Type = D3D12_CACHED_PIPELINE_STATE; };
	template<> struct CD3DX12_PIPELINE_STATE_SUBOJECT_TYPE_TRAITS<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING> { using Type = CD3DX12_VIEW_INSTANCING_DESC; };

	template<> struct CD3DX12_PIPELINE_STATE_SUBOJECT_TYPE_TRAITS<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS> { using Type = D3D12_SHADER_BYTECODE; };
	template<> struct CD3DX12_PIPELINE_STATE_SUBOJECT_TYPE_TRAITS<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS> { using Type = D3D12_SHADER_BYTECODE; };

public:
	CD3DX12_PIPELINE_STATE_STREAM_HELPER()
	{
		m_pSubobjectData = new char[sizeof(CD3DX12_PIPELINE_STATE_STREAM2)];
		m_pSubobjectLocations = new int[D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MAX_VALID];
		memset(m_pSubobjectData, 0, sizeof(CD3DX12_PIPELINE_STATE_STREAM2));
		memset(m_pSubobjectLocations, -1, sizeof(int) * D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MAX_VALID);
	}

	CD3DX12_PIPELINE_STATE_STREAM_HELPER(const CD3DX12_PIPELINE_STATE_STREAM_HELPER& rhs)
		: m_Size(rhs.m_Size), m_Subobjects(rhs.m_Subobjects)
	{
		m_pSubobjectData = new char[sizeof(CD3DX12_PIPELINE_STATE_STREAM2)];
		m_pSubobjectLocations = new int[D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MAX_VALID];
		memcpy(m_pSubobjectData, rhs.m_pSubobjectData, sizeof(CD3DX12_PIPELINE_STATE_STREAM2));
		memcpy(m_pSubobjectLocations, rhs.m_pSubobjectLocations, sizeof(int) * D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MAX_VALID);
	}

	CD3DX12_PIPELINE_STATE_STREAM_HELPER& operator=(const CD3DX12_PIPELINE_STATE_STREAM_HELPER& rhs)
	{
		m_Size = rhs.m_Size;
		m_Subobjects = rhs.m_Subobjects;
		m_pSubobjectData = new char[sizeof(CD3DX12_PIPELINE_STATE_STREAM2)];
		m_pSubobjectLocations = new int[D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MAX_VALID];
		memcpy(m_pSubobjectData, rhs.m_pSubobjectData, sizeof(CD3DX12_PIPELINE_STATE_STREAM2));
		memcpy(m_pSubobjectLocations, rhs.m_pSubobjectLocations, sizeof(int) * D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MAX_VALID);
		return *this;
	}

	~CD3DX12_PIPELINE_STATE_STREAM_HELPER()
	{
		delete[] m_pSubobjectData;
		delete[] m_pSubobjectLocations;
	}

	D3D12_PIPELINE_STATE_STREAM_DESC Desc()
	{
		D3D12_PIPELINE_STATE_STREAM_DESC desc{};
		desc.pPipelineStateSubobjectStream = m_pSubobjectData;
		desc.SizeInBytes = m_Size;
		return desc;
	}

	// USAGE EXAMPLE
	// CD3DX12_DEPTH_STENCIL_DESC1* pDepthStencilData = Desc.GetSubobjectData<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1>()

	template<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE ObjectType>
	typename CD3DX12_PIPELINE_STATE_SUBOJECT_TYPE_TRAITS<ObjectType>::Type* GetSubobjectData()
	{
		using InnerType = typename CD3DX12_PIPELINE_STATE_SUBOJECT_TYPE_TRAITS<ObjectType>::Type;
		struct SubobjectType
		{
			D3D12_PIPELINE_STATE_SUBOBJECT_TYPE ObjType;
			InnerType ObjectData;
		};
		if (m_pSubobjectLocations[ObjectType] < 0)
		{
			SubobjectType* pType = (SubobjectType*)(m_pSubobjectData + m_Size);
			pType->ObjType = ObjectType;
			m_pSubobjectLocations[ObjectType] = m_Size;
			m_Size += Math::AlignUp<uint32>(sizeof(SubobjectType), sizeof(void*));
			m_Subobjects++;
		}
		int offset = m_pSubobjectLocations[ObjectType];
		SubobjectType* pObj = (SubobjectType*)&m_pSubobjectData[offset];
		return &pObj->ObjectData;
	}

private:
	int* m_pSubobjectLocations = nullptr;
	char* m_pSubobjectData = nullptr;
	uint32 m_Subobjects = 0;
	uint32 m_Size = 0;
};

#endif // defined( __cplusplus )

#endif //__D3DX12_EXTRA_H__
