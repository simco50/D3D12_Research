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

#include <string>
#include <vector>

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

class CD3DX12_STATE_OBJECT_HELPER
{
	class PODLinearAllocator
	{
	public:
		PODLinearAllocator(uint32 size)
			: m_Size(size), m_pData(new char[m_Size]), m_pCurrentData(m_pData)
		{
			memset(m_pData, 0, m_Size);
		}

		~PODLinearAllocator()
		{
			delete[] m_pData;
			m_pData = nullptr;
		}

		template<typename T>
		T* Allocate(int count = 1)
		{
			return (T*)Allocate(sizeof(T) * count);
		}

		char* Allocate(uint32 size)
		{
			assert(size > 0);
			assert(m_pCurrentData - m_pData - size <= m_Size && "Make allocator size larger");
			char* pData = m_pCurrentData;
			m_pCurrentData += size;
			return pData;
		}

		const char* Data() const { return m_pData; }

	private:
		uint32 m_Size;
		char* m_pData;
		char* m_pCurrentData;
	};
	wchar_t* GetUnicode(const char* pText)
	{
		int len = (int)strlen(pText);
		wchar_t* pData = m_ScratchAllocator.Allocate<wchar_t>(len + 1);
		MultiByteToWideChar(0, 0, pText, len, pData, len);
		return pData;
	}
public:
	CD3DX12_STATE_OBJECT_HELPER(D3D12_STATE_OBJECT_TYPE type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE)
		: m_StateObjectAllocator(0xFF), m_ScratchAllocator(0xFFFF), m_Type(type)
	{}

	uint32 AddLibrary(const D3D12_SHADER_BYTECODE& byteCode, const std::vector<std::string>& exports = {}) 
	{
		D3D12_DXIL_LIBRARY_DESC* pDesc = m_ScratchAllocator.Allocate<D3D12_DXIL_LIBRARY_DESC>();
		pDesc->DXILLibrary = byteCode;
		if (exports.size())
		{
			D3D12_EXPORT_DESC* pExports = m_ScratchAllocator.Allocate<D3D12_EXPORT_DESC>((uint32)exports.size());
			D3D12_EXPORT_DESC* pCurrentExport = pExports;
			for (const std::string& exportName : exports)
			{
				wchar_t* pNameData = GetUnicode(exportName.c_str());
				pCurrentExport->ExportToRename = pNameData;
				pCurrentExport->Name = pNameData;
				pCurrentExport->Flags = D3D12_EXPORT_FLAG_NONE;
				pCurrentExport++;
			}
			pDesc->NumExports = (uint32)exports.size();
			pDesc->pExports = pExports;
		}
		return AddStateObject(pDesc, D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY);
	}
	uint32 AddHitGroup(const char* pHitGroupExport, const char* pClosestHitShaderImport = nullptr, const char* pAnyHitShaderImport = nullptr, const char* pIntersectionShaderImport = nullptr)
	{
		assert(pHitGroupExport);
		D3D12_HIT_GROUP_DESC* pDesc = m_ScratchAllocator.Allocate<D3D12_HIT_GROUP_DESC>();
		pDesc->HitGroupExport = GetUnicode(pHitGroupExport);
		if(pClosestHitShaderImport)
			pDesc->ClosestHitShaderImport = GetUnicode(pClosestHitShaderImport);
		if (pAnyHitShaderImport)
			pDesc->AnyHitShaderImport = GetUnicode(pAnyHitShaderImport);
		if (pIntersectionShaderImport)
			pDesc->IntersectionShaderImport = GetUnicode(pIntersectionShaderImport);
		pDesc->Type = pIntersectionShaderImport ? D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE : D3D12_HIT_GROUP_TYPE_TRIANGLES;
		return AddStateObject(pDesc, D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP);
	}

	uint32 AddStateAssociation(uint32 index, const std::vector<std::string>& exports)
	{
		assert(exports.size());
		D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION* pAssociation = m_ScratchAllocator.Allocate<D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION>();
		pAssociation->NumExports = (uint32)exports.size();
		pAssociation->pSubobjectToAssociate = GetSubobject(index);
		const wchar_t** pExportList = m_ScratchAllocator.Allocate<const wchar_t*>(pAssociation->NumExports);
		pAssociation->pExports = pExportList;
		for (size_t i = 0; i < exports.size(); ++i)
		{
			pExportList[i] = GetUnicode(exports[i].c_str());;
		}
		return AddStateObject(pAssociation, D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION);
	}

	uint32 AddCollection(ID3D12StateObject* pStateObject, const std::vector<std::string>& exports = {})
	{
		D3D12_EXISTING_COLLECTION_DESC* pDesc = m_ScratchAllocator.Allocate<D3D12_EXISTING_COLLECTION_DESC>();
		pDesc->pExistingCollection = pStateObject;
		if (exports.size())
		{
			D3D12_EXPORT_DESC* pExports = m_ScratchAllocator.Allocate<D3D12_EXPORT_DESC>((uint32)exports.size());
			pDesc->pExports = pExports;
			for (size_t i = 0; i < exports.size(); ++i)
			{
				wchar_t* pNameData = GetUnicode(exports[i].c_str());
				D3D12_EXPORT_DESC& currentExport = pExports[i];
				currentExport.ExportToRename = pNameData;
				currentExport.Name = pNameData;
				currentExport.Flags = D3D12_EXPORT_FLAG_NONE;
			}
		}
		return AddStateObject(pDesc, D3D12_STATE_SUBOBJECT_TYPE_EXISTING_COLLECTION);
	}

	uint32 BindLocalRootSignature(const char* pExportName, ID3D12RootSignature* pRootSignature)
	{
		D3D12_LOCAL_ROOT_SIGNATURE* pRs = m_ScratchAllocator.Allocate<D3D12_LOCAL_ROOT_SIGNATURE>();
		pRs->pLocalRootSignature = pRootSignature;
		uint32 rsState = AddStateObject(pRs, D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE);
		return AddStateAssociation(rsState, { pExportName });
	}

	uint32 SetRaytracingShaderConfig(uint32 maxPayloadSize, uint32 maxAttributeSize)
	{
		D3D12_RAYTRACING_SHADER_CONFIG* pDesc = m_ScratchAllocator.Allocate<D3D12_RAYTRACING_SHADER_CONFIG>();
		pDesc->MaxPayloadSizeInBytes = maxPayloadSize;
		pDesc->MaxAttributeSizeInBytes = maxAttributeSize;
		return AddStateObject(pDesc, D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG);
	}

	uint32 SetRaytracingPipelineConfig(uint32 maxRecursionDepth)
	{
		D3D12_RAYTRACING_PIPELINE_CONFIG* pDesc = m_ScratchAllocator.Allocate<D3D12_RAYTRACING_PIPELINE_CONFIG>();
		pDesc->MaxTraceRecursionDepth = maxRecursionDepth;
		return AddStateObject(pDesc, D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG);
	}

	uint32 SetRaytracingPipelineConfig1(uint32 maxRecursionDepth, D3D12_RAYTRACING_PIPELINE_FLAGS flags)
	{
		D3D12_RAYTRACING_PIPELINE_CONFIG1* pDesc = m_ScratchAllocator.Allocate<D3D12_RAYTRACING_PIPELINE_CONFIG1>();
		pDesc->MaxTraceRecursionDepth = maxRecursionDepth;
		pDesc->Flags = flags;
		return AddStateObject(pDesc, D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG1);
	}

	uint32 SetGlobalRootSignature(ID3D12RootSignature* pRootSignature)
	{
		D3D12_GLOBAL_ROOT_SIGNATURE* pRs = m_ScratchAllocator.Allocate<D3D12_GLOBAL_ROOT_SIGNATURE>();
		pRs->pGlobalRootSignature = pRootSignature;
		return AddStateObject(pRs, D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE);
	}

	void SetStateObjectConfig(D3D12_STATE_OBJECT_FLAGS flags)
	{
		D3D12_STATE_OBJECT_CONFIG* pConfig = m_ScratchAllocator.Allocate<D3D12_STATE_OBJECT_CONFIG>();
		pConfig->Flags = flags;
		AddStateObject(pConfig, D3D12_STATE_SUBOBJECT_TYPE_STATE_OBJECT_CONFIG);
	}

	D3D12_STATE_OBJECT_DESC Desc()
	{
		D3D12_STATE_OBJECT_DESC desc{};
		desc.NumSubobjects = m_SubObjects;
		desc.Type = m_Type;
		desc.pSubobjects = (D3D12_STATE_SUBOBJECT*)m_StateObjectAllocator.Data();
		return desc;
	}

private:
	uint32 AddStateObject(void* pDesc, D3D12_STATE_SUBOBJECT_TYPE type)
	{
		D3D12_STATE_SUBOBJECT* pState = m_StateObjectAllocator.Allocate<D3D12_STATE_SUBOBJECT>();
		pState->pDesc = pDesc;
		pState->Type = type;
		return m_SubObjects++;
	}

	const D3D12_STATE_SUBOBJECT* GetSubobject(uint32 index) const
	{
		assert(index < m_SubObjects);
		const D3D12_STATE_SUBOBJECT* pData = (D3D12_STATE_SUBOBJECT*)m_StateObjectAllocator.Data();
		return &pData[index];
	}

	PODLinearAllocator m_StateObjectAllocator;
	PODLinearAllocator m_ScratchAllocator;
	uint32 m_SubObjects = 0;
	D3D12_STATE_OBJECT_TYPE m_Type;
};

class CD3DX12_PIPELINE_STATE_STREAM_HELPER
{
private:
	template<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE T> struct CD3DX12_PIPELINE_STATE_SUBOJECT_TYPE_TRAITS;
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
		: m_Subobjects(rhs.m_Subobjects), m_Size(rhs.m_Size)
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
	typename CD3DX12_PIPELINE_STATE_SUBOJECT_TYPE_TRAITS<ObjectType>::Type& GetSubobject()
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
			const auto AlignUp = [](uint32 value, uint32 alignment) {return (value + (alignment - 1)) & ~(alignment - 1); };
			m_Size += AlignUp(sizeof(SubobjectType), sizeof(void*));
			m_Subobjects++;
		}
		int offset = m_pSubobjectLocations[ObjectType];
		SubobjectType* pObj = (SubobjectType*)&m_pSubobjectData[offset];
		return pObj->ObjectData;
	}

private:
	int* m_pSubobjectLocations = nullptr;
	char* m_pSubobjectData = nullptr;
	uint32 m_Subobjects = 0;
	uint32 m_Size = 0;
};

#endif //__D3DX12_EXTRA_H__
