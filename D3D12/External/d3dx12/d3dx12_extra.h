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
#include <iomanip>

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
		: m_StateObjectAllocator(0xFFF), m_ScratchAllocator(0xFFFF), m_Type(type)
	{}

	uint32 AddLibrary(const D3D12_SHADER_BYTECODE& byteCode, const char** pInExports, uint32 numExports) 
	{
		D3D12_DXIL_LIBRARY_DESC* pDesc = m_ScratchAllocator.Allocate<D3D12_DXIL_LIBRARY_DESC>();
		pDesc->DXILLibrary = byteCode;
		if (numExports)
		{
			D3D12_EXPORT_DESC* pExports = m_ScratchAllocator.Allocate<D3D12_EXPORT_DESC>(numExports);
			D3D12_EXPORT_DESC* pCurrentExport = pExports;
			for (uint32 i = 0; i < numExports; ++i)
			{
				wchar_t* pNameData = GetUnicode(pInExports[i]);
				pCurrentExport->ExportToRename = pNameData;
				pCurrentExport->Name = pNameData;
				pCurrentExport->Flags = D3D12_EXPORT_FLAG_NONE;
				pCurrentExport++;
			}
			pDesc->NumExports = numExports;
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

	static std::string DebugPrint(const D3D12_STATE_OBJECT_DESC* desc)
	{
		std::wstringstream wstr;
		wstr << L"--------------------------------------------------------------------\n";
		wstr << L"| D3D12 State Object 0x" << static_cast<const void*>(desc) << L": ";
		if (desc->Type == D3D12_STATE_OBJECT_TYPE_COLLECTION) wstr << L"Collection\n";
		if (desc->Type == D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE) wstr << L"Raytracing Pipeline\n";

		auto ExportTree = [](UINT depth, UINT numExports, const D3D12_EXPORT_DESC* exports)
		{
			std::wostringstream woss;
			for (UINT i = 0; i < numExports; i++)
			{
				woss << L"|";
				if (depth > 0)
				{
					for (UINT j = 0; j < 2 * depth - 1; j++) woss << L" ";
				}
				woss << L" [" << i << L"]: ";
				if (exports[i].ExportToRename) woss << exports[i].ExportToRename << L" --> ";
				woss << exports[i].Name << L"\n";
			}
			return woss.str();
		};

		for (UINT i = 0; i < desc->NumSubobjects; i++)
		{
			wstr << L"| [" << i << L"]: ";
			switch (desc->pSubobjects[i].Type)
			{
			case D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE:
				wstr << L"Global Root Signature 0x" << desc->pSubobjects[i].pDesc << L"\n";
				break;
			case D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE:
				wstr << L"Local Root Signature 0x" << desc->pSubobjects[i].pDesc << L"\n";
				break;
			case D3D12_STATE_SUBOBJECT_TYPE_NODE_MASK:
				wstr << L"Node Mask: 0x" << std::hex << std::setfill(L'0') << std::setw(8) << *static_cast<const UINT*>(desc->pSubobjects[i].pDesc) << std::setw(0) << std::dec << L"\n";
				break;
			case D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY:
			{
				wstr << L"DXIL Library 0x";
				auto lib = static_cast<const D3D12_DXIL_LIBRARY_DESC*>(desc->pSubobjects[i].pDesc);
				wstr << lib->DXILLibrary.pShaderBytecode << L", " << lib->DXILLibrary.BytecodeLength << L" bytes\n";
				wstr << ExportTree(1, lib->NumExports, lib->pExports);
				break;
			}
			case D3D12_STATE_SUBOBJECT_TYPE_EXISTING_COLLECTION:
			{
				wstr << L"Existing Library 0x";
				auto collection = static_cast<const D3D12_EXISTING_COLLECTION_DESC*>(desc->pSubobjects[i].pDesc);
				wstr << collection->pExistingCollection << L"\n";
				wstr << ExportTree(1, collection->NumExports, collection->pExports);
				break;
			}
			case D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION:
			{
				wstr << L"Subobject to Exports Association (Subobject [";
				auto association = static_cast<const D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION*>(desc->pSubobjects[i].pDesc);
				UINT index = static_cast<UINT>(association->pSubobjectToAssociate - desc->pSubobjects);
				wstr << index << L"])\n";
				for (UINT j = 0; j < association->NumExports; j++)
				{
					wstr << L"|  [" << j << L"]: " << association->pExports[j] << L"\n";
				}
				break;
			}
			case D3D12_STATE_SUBOBJECT_TYPE_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION:
			{
				wstr << L"DXIL Subobjects to Exports Association (";
				auto association = static_cast<const D3D12_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION*>(desc->pSubobjects[i].pDesc);
				wstr << association->SubobjectToAssociate << L")\n";
				for (UINT j = 0; j < association->NumExports; j++)
				{
					wstr << L"|  [" << j << L"]: " << association->pExports[j] << L"\n";
				}
				break;
			}
			case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG:
			{
				wstr << L"Raytracing Shader Config\n";
				auto config = static_cast<const D3D12_RAYTRACING_SHADER_CONFIG*>(desc->pSubobjects[i].pDesc);
				wstr << L"|  [0]: Max Payload Size: " << config->MaxPayloadSizeInBytes << L" bytes\n";
				wstr << L"|  [1]: Max Attribute Size: " << config->MaxAttributeSizeInBytes << L" bytes\n";
				break;
			}
			case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG:
			{
				wstr << L"Raytracing Pipeline Config\n";
				auto config = static_cast<const D3D12_RAYTRACING_PIPELINE_CONFIG*>(desc->pSubobjects[i].pDesc);
				wstr << L"|  [0]: Max Recursion Depth: " << config->MaxTraceRecursionDepth << L"\n";
				break;
			}
			case D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP:
			{
				wstr << L"Hit Group (";
				auto hitGroup = static_cast<const D3D12_HIT_GROUP_DESC*>(desc->pSubobjects[i].pDesc);
				wstr << (hitGroup->HitGroupExport ? hitGroup->HitGroupExport : L"[none]") << L")\n";
				wstr << L"|  [0]: Any Hit Import: " << (hitGroup->AnyHitShaderImport ? hitGroup->AnyHitShaderImport : L"[none]") << L"\n";
				wstr << L"|  [1]: Closest Hit Import: " << (hitGroup->ClosestHitShaderImport ? hitGroup->ClosestHitShaderImport : L"[none]") << L"\n";
				wstr << L"|  [2]: Intersection Import: " << (hitGroup->IntersectionShaderImport ? hitGroup->IntersectionShaderImport : L"[none]") << L"\n";
				break;
			}
			}
			wstr << L"|--------------------------------------------------------------------\n";
		}
		std::wstring woutput = wstr.str();
		
		size_t size = 0;
		wcstombs_s(&size, nullptr, 0, woutput.c_str(), 4096);
		char* aOutput = new char[size];
		wcstombs_s(&size, aOutput, size, woutput.c_str(), 4096);
		std::string result = aOutput;
		delete[] aOutput;
		return result;
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

#define SUBOBJECT_TRAIT(value, type) \
	template<> struct CD3DX12_PIPELINE_STATE_SUBOJECT_TYPE_TRAITS<value> \
	{ \
		using Type = type; \
	};

	template<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE T> struct CD3DX12_PIPELINE_STATE_SUBOJECT_TYPE_TRAITS;
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS, D3D12_PIPELINE_STATE_FLAGS)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK, UINT)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE, ID3D12RootSignature*)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT, D3D12_INPUT_LAYOUT_DESC)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE, D3D12_INDEX_BUFFER_STRIP_CUT_VALUE)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY, D3D12_PRIMITIVE_TOPOLOGY_TYPE)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS, D3D12_SHADER_BYTECODE)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS, D3D12_SHADER_BYTECODE)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT, D3D12_STREAM_OUTPUT_DESC)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS, D3D12_SHADER_BYTECODE)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS, D3D12_SHADER_BYTECODE)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, D3D12_SHADER_BYTECODE)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS, D3D12_SHADER_BYTECODE)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND, CD3DX12_BLEND_DESC)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL, CD3DX12_DEPTH_STENCIL_DESC)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1, CD3DX12_DEPTH_STENCIL_DESC1)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT, DXGI_FORMAT)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER, CD3DX12_RASTERIZER_DESC)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS, D3D12_RT_FORMAT_ARRAY)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC, DXGI_SAMPLE_DESC)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK, UINT)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO, D3D12_CACHED_PIPELINE_STATE)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING, CD3DX12_VIEW_INSTANCING_DESC)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS, D3D12_SHADER_BYTECODE)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS, D3D12_SHADER_BYTECODE)

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

	std::string DebugPrint()
	{
		std::stringstream str;
		str << "---------------------------------\n";
		str << "| D3D12 Pipeline State Stream |\n";

		for (uint32 i = 0; i < D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MAX_VALID; ++i)
		{
			int offset = m_pSubobjectLocations[i];
			if (offset >= 0)
			{
				str << "| [" << i << "]: ";

				struct SubobjectType
				{
					D3D12_PIPELINE_STATE_SUBOBJECT_TYPE ObjType;
					void* ObjectData;
				};
				SubobjectType* pSubObject = (SubobjectType*)&m_pSubobjectData[offset];
				void* pSubObjectData = &pSubObject->ObjectData;

				switch (pSubObject->ObjType)
				{
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS:
				{
					D3D12_PIPELINE_STATE_FLAGS* pObjectData = reinterpret_cast<D3D12_PIPELINE_STATE_FLAGS*>(pSubObjectData);
					str << "Flags: " << *pObjectData;
					break;
				}
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK:
				{
					UINT* pObjectData = reinterpret_cast<UINT*>(pSubObjectData);
					str << "Node Mask: " << *pObjectData;
					break;
				}
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE:
				{
					ID3D12RootSignature** pObjectData = reinterpret_cast<ID3D12RootSignature**>(pSubObjectData);
					str << "Root Signature: " << *pObjectData;
					break;
				}
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT:
				{
					D3D12_INPUT_LAYOUT_DESC* pObjectData = reinterpret_cast<D3D12_INPUT_LAYOUT_DESC*>(pSubObjectData);
					str << "Input Layout: \n";
					str << "\tElements: " << pObjectData->NumElements;
					for (uint32 elementIndex = 0; elementIndex < pObjectData->NumElements; ++elementIndex)
					{
						str << "\t[" << elementIndex << "] " << pObjectData->pInputElementDescs[elementIndex].SemanticName << pObjectData->pInputElementDescs[elementIndex].SemanticIndex;
					}
					break;
				}
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE:
				{
					D3D12_INDEX_BUFFER_STRIP_CUT_VALUE* pObjectData = reinterpret_cast<D3D12_INDEX_BUFFER_STRIP_CUT_VALUE*>(pSubObjectData);
					str << "Index Buffer Strip Cut Value: " << *pObjectData;
					break;
				}
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY:
				{
					D3D12_PRIMITIVE_TOPOLOGY_TYPE* pObjectData = reinterpret_cast<D3D12_PRIMITIVE_TOPOLOGY_TYPE*>(pSubObjectData);
					str << "Primitive Topology: " << *pObjectData;
					break;
				}
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS:
				{
					D3D12_SHADER_BYTECODE* pObjectData = reinterpret_cast<D3D12_SHADER_BYTECODE*>(pSubObjectData);
					str << "Vertex Shader - ByteCode: 0x" << pObjectData->pShaderBytecode << " - Length: " << pObjectData->BytecodeLength << " bytes";
					break;
				}
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS:
				{
					D3D12_SHADER_BYTECODE* pObjectData = reinterpret_cast<D3D12_SHADER_BYTECODE*>(pSubObjectData);
					str << "Geometry Shader - ByteCode: 0x" << pObjectData->pShaderBytecode << " - Length: " << pObjectData->BytecodeLength << " bytes";
					break;
				}
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS:
				{
					D3D12_SHADER_BYTECODE* pObjectData = reinterpret_cast<D3D12_SHADER_BYTECODE*>(pSubObjectData);
					str << "Pixel Shader - ByteCode: 0x" << pObjectData->pShaderBytecode << " - Length: " << pObjectData->BytecodeLength << " bytes";
					break;
				}
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS:
				{
					D3D12_SHADER_BYTECODE* pObjectData = reinterpret_cast<D3D12_SHADER_BYTECODE*>(pSubObjectData);
					str << "Compute Shader - ByteCode: 0x" << pObjectData->pShaderBytecode << " - Length: " << pObjectData->BytecodeLength << " bytes";
					break;
				}
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS:
				{
					D3D12_SHADER_BYTECODE* pObjectData = reinterpret_cast<D3D12_SHADER_BYTECODE*>(pSubObjectData);
					str << "Mesh Shader - ByteCode: 0x" << pObjectData->pShaderBytecode << " - Length: " << pObjectData->BytecodeLength << " bytes";
					break;
				}
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS:
				{
					D3D12_SHADER_BYTECODE* pObjectData = reinterpret_cast<D3D12_SHADER_BYTECODE*>(pSubObjectData);
					str << "Amplification Shader - ByteCode: 0x" << pObjectData->pShaderBytecode << " - Length: " << pObjectData->BytecodeLength << " bytes";
					break;
				}
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS:
				{
					D3D12_SHADER_BYTECODE* pObjectData = reinterpret_cast<D3D12_SHADER_BYTECODE*>(pSubObjectData);
					str << "Hull Shader - ByteCode: 0x" << pObjectData->pShaderBytecode << " - Length: " << pObjectData->BytecodeLength << " bytes";
					break;
				}
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS:
				{
					D3D12_SHADER_BYTECODE* pObjectData = reinterpret_cast<D3D12_SHADER_BYTECODE*>(pSubObjectData);
					str << "Domain Shader - ByteCode: 0x" << pObjectData->pShaderBytecode << " - Length: " << pObjectData->BytecodeLength << " bytes";
					break;
				}
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT:
				{
					//D3D12_STREAM_OUTPUT_DESC* pObjectData = reinterpret_cast<D3D12_STREAM_OUTPUT_DESC*>(pSubObjectData);
					str << "Stream Output";
					break;
				}
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND:
				{
					//CD3DX12_BLEND_DESC* pObjectData = reinterpret_cast<CD3DX12_BLEND_DESC*>(pSubObjectData);
					str << "Blend Desc";
					break;
				}
				}

				str << "\n";
			}
		}
		str << "|--------------------------------------------------------------------\n";
		return str.str();
	}

private:
	int* m_pSubobjectLocations = nullptr;
	char* m_pSubobjectData = nullptr;
	uint32 m_Subobjects = 0;
	uint32 m_Size = 0;
};

#endif //__D3DX12_EXTRA_H__
