#include "stdafx.h"
#include "StateObject.h"
#include "Shader.h"
#include "RootSignature.h"
#include "Graphics.h"

StateObject::StateObject(Graphics* pGraphics)
	: GraphicsObject(pGraphics)
{

}

void StateObject::Create(const StateObjectInitializer& initializer)
{
	m_Desc = initializer;
	D3D12_STATE_OBJECT_DESC desc = m_Desc.Desc();
	VERIFY_HR(GetParent()->GetRaytracingDevice()->CreateStateObject(&desc, IID_PPV_ARGS(m_pStateObject.ReleaseAndGetAddressOf())));
	D3D::SetObjectName(m_pStateObject.Get(), m_Desc.Name.c_str());
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

void StateObjectInitializer::SetRayGenShader(const std::string& exportName)
{
	RayGenShader = exportName;
}

D3D12_STATE_OBJECT_DESC StateObjectInitializer::Desc()
{
	m_StateObjectData.Reset();
	m_ContentData.Reset();
	uint32 numObjects = 0;
	auto AddStateObject = [this, &numObjects](void* pDesc, D3D12_STATE_SUBOBJECT_TYPE type)
	{
		D3D12_STATE_SUBOBJECT* pState = m_StateObjectData.Allocate<D3D12_STATE_SUBOBJECT>();
		pState->pDesc = pDesc;
		pState->Type = type;
		++numObjects;
		return pState;
	};

	for (const LibraryExports& library : m_Libraries)
	{
		D3D12_DXIL_LIBRARY_DESC* pDesc = m_ContentData.Allocate<D3D12_DXIL_LIBRARY_DESC>();
		pDesc->DXILLibrary = CD3DX12_SHADER_BYTECODE(library.pLibrary->GetByteCode(), library.pLibrary->GetByteCodeSize());
		if (library.Exports.size())
		{
			D3D12_EXPORT_DESC* pExports = m_ContentData.Allocate<D3D12_EXPORT_DESC>((int)library.Exports.size());
			D3D12_EXPORT_DESC* pCurrentExport = pExports;
			for (uint32 i = 0; i < library.Exports.size(); ++i)
			{
				wchar_t* pNameData = GetUnicode(library.Exports[i]);
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
		D3D12_HIT_GROUP_DESC* pDesc = m_ContentData.Allocate<D3D12_HIT_GROUP_DESC>();
		pDesc->HitGroupExport = GetUnicode(hitGroup.Name);
		if (!hitGroup.ClosestHit.empty())
			pDesc->ClosestHitShaderImport = GetUnicode(hitGroup.ClosestHit);
		if (!hitGroup.AnyHit.empty())
			pDesc->AnyHitShaderImport = GetUnicode(hitGroup.AnyHit);
		if (!hitGroup.Intersection.empty())
			pDesc->IntersectionShaderImport = GetUnicode(hitGroup.Intersection);
		pDesc->Type = !hitGroup.Intersection.empty() ? D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE : D3D12_HIT_GROUP_TYPE_TRIANGLES;
		AddStateObject(pDesc, D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP);

		if (hitGroup.pLocalRootSignature)
		{
			D3D12_LOCAL_ROOT_SIGNATURE* pRs = m_ContentData.Allocate<D3D12_LOCAL_ROOT_SIGNATURE>();
			pRs->pLocalRootSignature = hitGroup.pLocalRootSignature->GetRootSignature();
			D3D12_STATE_SUBOBJECT* pSubObject = AddStateObject(pRs, D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE);

			D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION* pAssociation = m_ContentData.Allocate<D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION>();
			pAssociation->NumExports = 1;
			pAssociation->pSubobjectToAssociate = pSubObject;
			const wchar_t** pExportList = m_ContentData.Allocate<const wchar_t*>(1);
			pExportList[0] = GetUnicode(hitGroup.Name);
			pAssociation->pExports = pExportList;
			AddStateObject(pAssociation, D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION);
		}
	}
	for (const LibraryShaderExport& missShader : m_MissShaders)
	{
		if (missShader.pLocalRootSignature)
		{
			D3D12_LOCAL_ROOT_SIGNATURE* pRs = m_ContentData.Allocate<D3D12_LOCAL_ROOT_SIGNATURE>();
			pRs->pLocalRootSignature = missShader.pLocalRootSignature->GetRootSignature();
			D3D12_STATE_SUBOBJECT* pSubObject = AddStateObject(pRs, D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE);

			D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION* pAssociation = m_ContentData.Allocate<D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION>();
			pAssociation->NumExports = 1;
			pAssociation->pSubobjectToAssociate = pSubObject;
			const wchar_t** pExportList = m_ContentData.Allocate<const wchar_t*>(1);
			pExportList[0] = GetUnicode(missShader.Name);
			pAssociation->pExports = pExportList;
			AddStateObject(pAssociation, D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION);
		}
	}

	if (Flags != D3D12_STATE_OBJECT_FLAG_NONE)
	{
		D3D12_RAYTRACING_PIPELINE_CONFIG1* pDesc = m_ContentData.Allocate<D3D12_RAYTRACING_PIPELINE_CONFIG1>();
		pDesc->MaxTraceRecursionDepth = MaxRecursion;
		pDesc->Flags = Flags;
		AddStateObject(pDesc, D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG1);
	}
	else
	{
		D3D12_RAYTRACING_PIPELINE_CONFIG* pDesc = m_ContentData.Allocate<D3D12_RAYTRACING_PIPELINE_CONFIG>();
		pDesc->MaxTraceRecursionDepth = MaxRecursion;
		AddStateObject(pDesc, D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG);
	}

	D3D12_GLOBAL_ROOT_SIGNATURE* pRs = m_ContentData.Allocate<D3D12_GLOBAL_ROOT_SIGNATURE>();
	pRs->pGlobalRootSignature = pGlobalRootSignature->GetRootSignature();
	AddStateObject(pRs, D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE);

	D3D12_RAYTRACING_SHADER_CONFIG* pDesc = m_ContentData.Allocate<D3D12_RAYTRACING_SHADER_CONFIG>();
	pDesc->MaxPayloadSizeInBytes = MaxPayloadSize;
	pDesc->MaxAttributeSizeInBytes = MaxAttributeSize;
	AddStateObject(pDesc, D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG);

	D3D12_STATE_OBJECT_DESC desc;
	desc.Type = Type;
	desc.NumSubobjects = numObjects;
	desc.pSubobjects = (D3D12_STATE_SUBOBJECT*)m_StateObjectData.GetData();
	return desc;
}
