#include "stdafx.h"
#include "StateObject.h"
#include "Shader.h"
#include "RootSignature.h"
#include "Graphics.h"

StateObject::StateObject(ShaderManager* pShaderManager, GraphicsDevice* pParent)
	: GraphicsObject(pParent)
{
	m_ReloadHandle = pShaderManager->OnLibraryRecompiledEvent().AddRaw(this, &StateObject::OnLibraryReloaded);
}

void StateObject::Create(const StateObjectInitializer& initializer)
{
	m_Desc = initializer;
	StateObjectInitializer::StateObjectStream stateObjectStream;
	m_Desc.CreateStateObjectStream(stateObjectStream);
	VERIFY_HR(GetParent()->GetRaytracingDevice()->CreateStateObject(&stateObjectStream.Desc, IID_PPV_ARGS(m_pStateObject.ReleaseAndGetAddressOf())));
	D3D::SetObjectName(m_pStateObject.Get(), m_Desc.Name.c_str());
	m_pStateObject.As(&m_pStateObjectProperties);
	//m_Desc.SetMaxPipelineStackSize(this); #todo: This is causing trouble with recursion!
}

void StateObject::ConditionallyReload()
{
	if (m_NeedsReload)
	{
		Create(m_Desc);
		m_NeedsReload = false;
		E_LOG(Info, "Reloaded State Object: %s", m_Desc.Name.c_str());
	}
}

void StateObject::OnLibraryReloaded(ShaderLibrary* pOldShaderLibrary, ShaderLibrary* pNewShaderLibrary)
{
	for (StateObjectInitializer::LibraryExports& library : m_Desc.m_Libraries)
	{
		if (library.pLibrary == pOldShaderLibrary)
		{
			library.pLibrary = pNewShaderLibrary;
			m_NeedsReload = true;
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

void StateObjectInitializer::AddLibrary(ShaderLibrary* pLibrary, const std::vector<std::string>& exports /*= {}*/)
{
	LibraryExports library;
	library.pLibrary = pLibrary;
	library.Exports = exports;
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


void StateObjectInitializer::CreateStateObjectStream(StateObjectStream& stateObjectStream)
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

	for (const LibraryExports& library : m_Libraries)
	{
		D3D12_DXIL_LIBRARY_DESC* pDesc = stateObjectStream.ContentData.Allocate<D3D12_DXIL_LIBRARY_DESC>();
		pDesc->DXILLibrary = CD3DX12_SHADER_BYTECODE(library.pLibrary->GetByteCode(), library.pLibrary->GetByteCodeSize());
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

	if (Flags != D3D12_RAYTRACING_PIPELINE_FLAG_NONE)
	{
		D3D12_RAYTRACING_PIPELINE_CONFIG1* pDesc = stateObjectStream.ContentData.Allocate<D3D12_RAYTRACING_PIPELINE_CONFIG1>();
		pDesc->MaxTraceRecursionDepth = MaxRecursion;
		pDesc->Flags = Flags;
		AddStateObject(pDesc, D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG1);
	}
	else
	{
		D3D12_RAYTRACING_PIPELINE_CONFIG* pDesc = stateObjectStream.ContentData.Allocate<D3D12_RAYTRACING_PIPELINE_CONFIG>();
		pDesc->MaxTraceRecursionDepth = MaxRecursion;
		AddStateObject(pDesc, D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG);
	}

	D3D12_GLOBAL_ROOT_SIGNATURE* pRs = stateObjectStream.ContentData.Allocate<D3D12_GLOBAL_ROOT_SIGNATURE>();
	pRs->pGlobalRootSignature = pGlobalRootSignature->GetRootSignature();
	AddStateObject(pRs, D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE);

	D3D12_RAYTRACING_SHADER_CONFIG* pDesc = stateObjectStream.ContentData.Allocate<D3D12_RAYTRACING_SHADER_CONFIG>();
	pDesc->MaxPayloadSizeInBytes = MaxPayloadSize;
	pDesc->MaxAttributeSizeInBytes = MaxAttributeSize;
	AddStateObject(pDesc, D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG);

	stateObjectStream.Desc.Type = Type;
	stateObjectStream.Desc.NumSubobjects = numObjects;
	stateObjectStream.Desc.pSubobjects = (D3D12_STATE_SUBOBJECT*)stateObjectStream.StateObjectData.GetData();
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
