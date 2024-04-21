#include "stdafx.h"
#include "StateObject.h"
#include "Shader.h"
#include "RootSignature.h"
#include "Graphics.h"

class StateObjectStream
{
	friend class StateObjectInitializer;
public:
	D3D12_STATE_OBJECT_DESC Desc;
private:
	template<size_t SIZE>
	struct DataAllocator
	{
		template<typename T>
		T* Allocate(uint32 count = 1)
		{
			assert(m_Offset + count * sizeof(T) <= SIZE);
			T* pData = reinterpret_cast<T*>(&m_Data[m_Offset]);
			m_Offset += count * sizeof(T);
			return pData;
		}
		void Reset() { m_Offset = 0; }
		const void* GetData() const { return m_Data.data(); }
		size_t Size() const { return m_Offset; }
	private:
		size_t m_Offset = 0;
		std::array<char, SIZE> m_Data{};
	};

	wchar_t* GetUnicode(const std::string& text)
	{
		size_t len = text.length();
		wchar_t* pData = ContentData.Allocate<wchar_t>((int)len + 1);
		MultiByteToWideChar(0, 0, text.c_str(), (int)len, pData, (int)len);
		return pData;
	}
	DataAllocator<1 << 8> StateObjectData{};
	DataAllocator<1 << 10> ContentData{};
};

StateObject::StateObject(GraphicsDevice* pParent, const StateObjectInitializer& initializer)
	: DeviceObject(pParent), m_Desc(initializer)
{
	m_ReloadHandle = pParent->GetShaderManager()->OnShaderEditedEvent().AddRaw(this, &StateObject::OnLibraryReloaded);
}

uint64 StateObject::GetWorkgraphBufferSize() const
{
	check(m_Desc.Type == D3D12_STATE_OBJECT_TYPE_EXECUTABLE);

	Ref<ID3D12WorkGraphProperties> pProps;
	m_pStateObject->QueryInterface(pProps.GetAddressOf());
	D3D12_WORK_GRAPH_MEMORY_REQUIREMENTS reqs{};
	pProps->GetWorkGraphMemoryRequirements(0, &reqs);
	return reqs.MaxSizeInBytes;
}

void StateObject::CreateInternal()
{
	StateObjectStream stateObjectStream;
	if (m_Desc.CreateStateObjectStream(stateObjectStream, GetParent()))
	{
		GetParent()->DeferReleaseObject(m_pStateObject.Detach());
		VERIFY_HR(GetParent()->GetDevice()->CreateStateObject(&stateObjectStream.Desc, IID_PPV_ARGS(m_pStateObject.ReleaseAndGetAddressOf())));
		D3D::SetObjectName(m_pStateObject, m_Desc.Name.c_str());
		VERIFY_HR(m_pStateObject->QueryInterface(m_pStateObjectProperties.ReleaseAndGetAddressOf()));
		//m_Desc.SetMaxPipelineStackSize(this); #todo: This is causing trouble with recursion!
	}
	else
	{
		E_LOG(Warning, "Failed to compile StateObject '%s'", m_Desc.Name);
	}
	E_LOG(Info, "Compiled State Object: %s", m_Desc.Name.c_str());
	check(m_pStateObject);
}

void StateObject::ConditionallyReload()
{
	if (m_NeedsReload || !m_pStateObject)
	{
		CreateInternal();
		m_NeedsReload = false;
	}
}

void StateObject::OnLibraryReloaded(Shader* pLibrary)
{
	for (Shader* pCurrentLibrary : m_Desc.m_Shaders)
	{
		if (pLibrary == pCurrentLibrary)
		{
			m_NeedsReload = true;
			break;
		}
	}
}

void StateObjectInitializer::AddHitGroup(const std::string& name, const std::string& closestHit /*= ""*/, const std::string& anyHit /*= ""*/, const std::string& intersection /*= ""*/, RootSignature* pRootSignature /*= nullptr*/)
{
	HitGroupDefinition definition;
	definition.Name = name;
	definition.AnyHit = anyHit;
	definition.ClosestHit = closestHit;
	definition.Intersection = intersection;
	definition.pLocalRootSignature = pRootSignature;
	m_HitGroups.push_back(definition);
}

void StateObjectInitializer::AddLibrary(const char* pShaderPath, Span<const char*> exports, Span<ShaderDefine> defines)
{
	LibraryExports library;
	library.Path = pShaderPath;
	library.Defines = defines.Copy();
	library.Exports = exports.Copy();
	m_Libraries.push_back(library);
}

void StateObjectInitializer::AddCollection(StateObject* pOtherObject)
{
	m_Collections.push_back(pOtherObject);
}

void StateObjectInitializer::AddMissShader(const std::string& exportName, RootSignature* pRootSignature /*= nullptr*/)
{
	LibraryShaderExport shader;
	shader.Name = exportName;
	shader.pLocalRootSignature = pRootSignature;
	m_MissShaders.push_back(shader);
}


bool StateObjectInitializer::CreateStateObjectStream(StateObjectStream& stateObjectStream, GraphicsDevice* pDevice)
{
	uint32 numObjects = 0;
	auto AddStateObject = [&stateObjectStream, &numObjects](void* pDesc, D3D12_STATE_SUBOBJECT_TYPE type)
	{
		D3D12_STATE_SUBOBJECT* pState = stateObjectStream.StateObjectData.Allocate<D3D12_STATE_SUBOBJECT>();
		pState->pDesc = pDesc;
		pState->Type = type;
		++numObjects;
		return pState;
	};

	std::vector<Shader*> shaders;
	for (const LibraryExports& library : m_Libraries)
	{
		D3D12_DXIL_LIBRARY_DESC* pDesc = stateObjectStream.ContentData.Allocate<D3D12_DXIL_LIBRARY_DESC>();
		Shader* pLibrary = pDevice->GetLibrary(library.Path.c_str(), library.Defines);
		if (!pLibrary)
			return false;

		shaders.push_back(pLibrary);
		pDesc->DXILLibrary = CD3DX12_SHADER_BYTECODE(pLibrary->pByteCode->GetBufferPointer(), pLibrary->pByteCode->GetBufferSize());
		if (library.Exports.size())
		{
			D3D12_EXPORT_DESC* pExports = stateObjectStream.ContentData.Allocate<D3D12_EXPORT_DESC>((int)library.Exports.size());
			D3D12_EXPORT_DESC* pCurrentExport = pExports;
			for (uint32 i = 0; i < library.Exports.size(); ++i)
			{
				wchar_t* pNameData = stateObjectStream.GetUnicode(library.Exports[i]);
				pCurrentExport->ExportToRename = pNameData;
				pCurrentExport->Name = pNameData;
				pCurrentExport->Flags = D3D12_EXPORT_FLAG_NONE;
				pCurrentExport++;
			}
			pDesc->NumExports = (uint32)library.Exports.size();
			pDesc->pExports = pExports;
		}
		AddStateObject(pDesc, D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY);
	}
	for (const HitGroupDefinition& hitGroup : m_HitGroups)
	{
		assert(!hitGroup.Name.empty());
		D3D12_HIT_GROUP_DESC* pDesc = stateObjectStream.ContentData.Allocate<D3D12_HIT_GROUP_DESC>();
		pDesc->HitGroupExport = stateObjectStream.GetUnicode(hitGroup.Name);
		if (!hitGroup.ClosestHit.empty())
			pDesc->ClosestHitShaderImport = stateObjectStream.GetUnicode(hitGroup.ClosestHit);
		if (!hitGroup.AnyHit.empty())
			pDesc->AnyHitShaderImport = stateObjectStream.GetUnicode(hitGroup.AnyHit);
		if (!hitGroup.Intersection.empty())
			pDesc->IntersectionShaderImport = stateObjectStream.GetUnicode(hitGroup.Intersection);
		pDesc->Type = !hitGroup.Intersection.empty() ? D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE : D3D12_HIT_GROUP_TYPE_TRIANGLES;
		AddStateObject(pDesc, D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP);

		if (hitGroup.pLocalRootSignature)
		{
			D3D12_LOCAL_ROOT_SIGNATURE* pRs = stateObjectStream.ContentData.Allocate<D3D12_LOCAL_ROOT_SIGNATURE>();
			pRs->pLocalRootSignature = hitGroup.pLocalRootSignature->GetRootSignature();
			D3D12_STATE_SUBOBJECT* pSubObject = AddStateObject(pRs, D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE);

			D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION* pAssociation = stateObjectStream.ContentData.Allocate<D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION>();
			pAssociation->NumExports = 1;
			pAssociation->pSubobjectToAssociate = pSubObject;
			const wchar_t** pExportList = stateObjectStream.ContentData.Allocate<const wchar_t*>(1);
			pExportList[0] = stateObjectStream.GetUnicode(hitGroup.Name);
			pAssociation->pExports = pExportList;
			AddStateObject(pAssociation, D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION);
		}
	}
	for (const LibraryShaderExport& missShader : m_MissShaders)
	{
		if (missShader.pLocalRootSignature)
		{
			D3D12_LOCAL_ROOT_SIGNATURE* pRs = stateObjectStream.ContentData.Allocate<D3D12_LOCAL_ROOT_SIGNATURE>();
			pRs->pLocalRootSignature = missShader.pLocalRootSignature->GetRootSignature();
			D3D12_STATE_SUBOBJECT* pSubObject = AddStateObject(pRs, D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE);

			D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION* pAssociation = stateObjectStream.ContentData.Allocate<D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION>();
			pAssociation->NumExports = 1;
			pAssociation->pSubobjectToAssociate = pSubObject;
			const wchar_t** pExportList = stateObjectStream.ContentData.Allocate<const wchar_t*>(1);
			pExportList[0] = stateObjectStream.GetUnicode(missShader.Name);
			pAssociation->pExports = pExportList;
			AddStateObject(pAssociation, D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION);
		}
	}

	if (Type == D3D12_STATE_OBJECT_TYPE_EXECUTABLE)
	{
		D3D12_WORK_GRAPH_DESC* pWG = stateObjectStream.ContentData.Allocate<D3D12_WORK_GRAPH_DESC>();
		pWG->Flags |= D3D12_WORK_GRAPH_FLAG_INCLUDE_ALL_AVAILABLE_NODES;
		pWG->ProgramName = stateObjectStream.GetUnicode(Name);
		AddStateObject(pWG, D3D12_STATE_SUBOBJECT_TYPE_WORK_GRAPH);
	}
	else if (Type == D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE)
	{
		D3D12_RAYTRACING_SHADER_CONFIG* pShaderConfig = stateObjectStream.ContentData.Allocate<D3D12_RAYTRACING_SHADER_CONFIG>();
		pShaderConfig->MaxPayloadSizeInBytes = MaxPayloadSize;
		pShaderConfig->MaxAttributeSizeInBytes = MaxAttributeSize;
		AddStateObject(pShaderConfig, D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG);

		D3D12_RAYTRACING_PIPELINE_CONFIG1* pRTConfig = stateObjectStream.ContentData.Allocate<D3D12_RAYTRACING_PIPELINE_CONFIG1>();
		pRTConfig->MaxTraceRecursionDepth = MaxRecursion;
		pRTConfig->Flags = Flags;
		AddStateObject(pRTConfig, D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG1);
	}

	D3D12_GLOBAL_ROOT_SIGNATURE* pRs = stateObjectStream.ContentData.Allocate<D3D12_GLOBAL_ROOT_SIGNATURE>();
	pRs->pGlobalRootSignature = pGlobalRootSignature->GetRootSignature();
	AddStateObject(pRs, D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE);



	stateObjectStream.Desc.Type = Type;
	stateObjectStream.Desc.NumSubobjects = numObjects;
	stateObjectStream.Desc.pSubobjects = (D3D12_STATE_SUBOBJECT*)stateObjectStream.StateObjectData.GetData();

	m_Shaders.swap(shaders);
	return true;
}

void StateObjectInitializer::SetMaxPipelineStackSize(StateObject* pStateObject)
{
	ID3D12StateObjectProperties* pProperties = pStateObject->GetStateObjectProperties();

	uint64 maxRayGenShader = pProperties->GetShaderStackSize(MULTIBYTE_TO_UNICODE(RayGenShader.c_str()));

	uint64 maxMissShader = 0;
	for (const LibraryShaderExport& missShader : m_MissShaders)
	{
		maxMissShader = Math::Max(maxMissShader, pProperties->GetShaderStackSize(MULTIBYTE_TO_UNICODE(missShader.Name.c_str())));
	}

	uint64 maxClosestHitShader = 0;
	uint64 maxIntersectionShader = 0;
	uint64 maxAnyHitShader = 0;
	for (const HitGroupDefinition& hitGroup : m_HitGroups)
	{
		wchar_t nameBuffer[256];
		uint32 offset = (uint32)StringConvert(hitGroup.Name.c_str(), nameBuffer, ARRAYSIZE(nameBuffer));

		StringConvert("::closesthit", &nameBuffer[offset - 1], 256 - offset);
		maxClosestHitShader = Math::Max(maxClosestHitShader, pProperties->GetShaderStackSize(nameBuffer));
		if (!hitGroup.AnyHit.empty())
		{
			StringConvert("::anyhit", &nameBuffer[offset - 1], 256 - offset);
			maxAnyHitShader = Math::Max(maxAnyHitShader, pProperties->GetShaderStackSize(nameBuffer));
		}
		if (!hitGroup.Intersection.empty())
		{
			StringConvert("::intersection", &nameBuffer[offset - 1], 256 - offset);
			maxIntersectionShader = Math::Max(maxIntersectionShader, pProperties->GetShaderStackSize(nameBuffer));
		}
	}

	uint64 maxSize = maxRayGenShader +
		Math::Max(Math::Max(maxClosestHitShader, maxMissShader), maxIntersectionShader + maxAnyHitShader) * Math::Min(1u, MaxRecursion) +
		Math::Max(maxClosestHitShader, maxMissShader) * Math::Max(MaxRecursion - 1, 0u);
	pProperties->SetPipelineStackSize(maxSize);
}
