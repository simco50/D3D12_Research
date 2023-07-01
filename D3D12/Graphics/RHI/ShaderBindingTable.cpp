#include "stdafx.h"
#include "StateObject.h"
#include "ShaderBindingTable.h"
#include "CommandContext.h"

uint32 ComputeRecordSize(uint32 size)
{
	return Math::AlignUp<uint32>(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + size, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
}

ShaderBindingTable::ShaderBindingTable(StateObject* pStateObject)
	: m_pStateObject(pStateObject)
{

}

void ShaderBindingTable::BindRayGenShader(const char* pName, const Span<uint64>& data /*= {}*/)
{
	uint32 dataSize = (uint32)data.GetSize() * sizeof(uint64);
	m_RayGenRecord = CreateRecord(pName, data.GetData(), dataSize);
	m_RayGenRecordSize = ComputeRecordSize(dataSize);
}

void ShaderBindingTable::BindMissShader(const char* pName, uint32 rayIndex, const Span<uint64>& data /*= {}*/)
{
	if (rayIndex >= (uint32)m_MissShaderRecords.size())
	{
		m_MissShaderRecords.resize(rayIndex + 1);
	}

	uint32 dataSize = (uint32)data.GetSize() * sizeof(uint64);
	m_MissShaderRecords[rayIndex] = CreateRecord(pName, data.GetData(), dataSize);
	m_MissRecordSize = Math::Max<int>(m_MissRecordSize, ComputeRecordSize(dataSize));
}

void ShaderBindingTable::BindHitGroup(const char* pName, uint32 index, const Span<uint64>& data /*= {}*/)
{
	BindHitGroup(pName, index, data.GetData(), (uint32)data.GetSize() * sizeof(uint64));
}

void ShaderBindingTable::BindHitGroup(const char* pName, uint32 index, const void* pData, uint32 dataSize)
{
	if (index >= m_HitGroupShaderRecords.size())
	{
		m_HitGroupShaderRecords.resize(index + 1);
	}
	m_HitGroupShaderRecords[index] = CreateRecord(pName, pData, dataSize);
	m_HitRecordSize = Math::Max<int>(m_HitRecordSize, ComputeRecordSize(dataSize));
}

void ShaderBindingTable::Commit(CommandContext& context, D3D12_DISPATCH_RAYS_DESC& desc)
{
	uint32 totalSize = 0;
	uint32 rayGenSection = m_RayGenRecordSize;
	uint32 rayGenSectionAligned = Math::AlignUp<uint32>(rayGenSection, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
	uint32 missSection = m_MissRecordSize * (uint32)m_MissShaderRecords.size();
	uint32 missSectionAligned = Math::AlignUp<uint32>(missSection, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
	uint32 hitSection = m_HitRecordSize * (uint32)m_HitGroupShaderRecords.size();
	uint32 hitSectionAligned = Math::AlignUp<uint32>(hitSection, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
	totalSize = Math::AlignUp<uint32>(rayGenSectionAligned + missSectionAligned + hitSectionAligned, 256);
	ScratchAllocation allocation = context.AllocateScratch(totalSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
	allocation.Clear();

	char* pStart = (char*)allocation.pMappedMemory;
	char* pData = pStart;

	// RayGen
	{
		memcpy(pData, m_RayGenRecord.pIdentifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
		memcpy(pData + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, m_RayGenRecord.pData.get(), m_RayGenRecord.Size);
		pData += m_RayGenRecordSize;
	}
	pData = pStart + rayGenSectionAligned;

	// Miss
	for (const ShaderRecord& e : m_MissShaderRecords)
	{
		memcpy(pData, e.pIdentifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
		memcpy(pData + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, e.pData.get(), e.Size);
		pData += m_MissRecordSize;
	}
	pData = pStart + rayGenSectionAligned + missSectionAligned;

	// Hit
	for (const ShaderRecord& e : m_HitGroupShaderRecords)
	{
		memcpy(pData, e.pIdentifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
		memcpy(pData + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, e.pData.get(), e.Size);
		pData += m_HitRecordSize;
	}

	desc.RayGenerationShaderRecord.StartAddress = allocation.GpuHandle;
	desc.RayGenerationShaderRecord.SizeInBytes = rayGenSection;
	desc.MissShaderTable.StartAddress = allocation.GpuHandle + rayGenSectionAligned;
	desc.MissShaderTable.SizeInBytes = missSection;
	desc.MissShaderTable.StrideInBytes = m_MissRecordSize;
	desc.HitGroupTable.StartAddress = allocation.GpuHandle + rayGenSectionAligned + missSectionAligned;
	desc.HitGroupTable.SizeInBytes = hitSection;
	desc.HitGroupTable.StrideInBytes = m_HitRecordSize;

	m_RayGenRecordSize = 0;
	m_MissShaderRecords.clear();
	m_MissRecordSize = 0;
	m_HitGroupShaderRecords.clear();
	m_HitRecordSize = 0;
}

ShaderBindingTable::ShaderRecord ShaderBindingTable::CreateRecord(const char* pName, const void* pData, uint32 dataSize)
{
	ShaderRecord entry;
	if (pName)
	{
		auto it = m_IdentifierMap.find(pName);
		if (it == m_IdentifierMap.end())
		{
			m_IdentifierMap[pName] = m_pStateObject->GetStateObjectProperties()->GetShaderIdentifier(MULTIBYTE_TO_UNICODE(pName));
		}
		entry.pIdentifier = m_IdentifierMap[pName];
		check(entry.pIdentifier);
		entry.pData = std::make_unique<char[]>(dataSize);
		entry.Size = dataSize;
		memcpy(entry.pData.get(), pData, dataSize);
	}
	else
	{
		constexpr const void* NullEntry = (void*)"";
		entry.pIdentifier = NullEntry;
	}
	return entry;
}
