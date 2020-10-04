#pragma once

class ShaderBindingTable
{
private:
	struct TableEntry
	{
		std::vector<void*> data;
		void* pIdentifier = nullptr;
	};
public:
	ShaderBindingTable(ID3D12StateObject* pStateObject)
	{
		VERIFY_HR(pStateObject->QueryInterface(IID_PPV_ARGS(m_pObjectProperties.GetAddressOf())));
	}

	void AddRayGenEntry(const char* pName, const std::vector<void*>& data)
	{
		m_RayGenTable.push_back(CreateEntry(pName, data));
		uint32 entrySize = Math::AlignUp<uint32>(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + (uint32)data.size() * 8, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
		m_RayGenEntrySize = Math::Max<int>(m_RayGenEntrySize, entrySize);
	}

	void AddMissEntry(const char* pName, const std::vector<void*>& data)
	{
		m_MissTable.push_back(CreateEntry(pName, data));
		uint32 entrySize = Math::AlignUp<uint32>(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + (uint32)data.size() * 8, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
		m_MissEntrySize = Math::Max<int>(m_MissEntrySize, entrySize);
	}

	void AddHitGroupEntry(const char* pName, const std::vector<void*>& data)
	{
		m_HitTable.push_back(CreateEntry(pName, data));
		uint32 entrySize = Math::AlignUp<uint32>(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + (uint32)data.size() * 8, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
		m_HitEntrySize = Math::Max<int>(m_HitEntrySize, entrySize);
	}

	void Commit(CommandContext& context, D3D12_DISPATCH_RAYS_DESC& desc)
	{
		uint32 totalSize = 0;
		uint32 rayGenSection = m_RayGenEntrySize * (uint32)m_RayGenTable.size();
		uint32 rayGenSectionAligned = Math::AlignUp<uint32>(rayGenSection, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
		uint32 missSection = m_MissEntrySize * (uint32)m_MissTable.size();
		uint32 missSectionAligned = Math::AlignUp<uint32>(missSection, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
		uint32 hitSection = m_HitEntrySize * (uint32)m_HitTable.size();
		uint32 hitSectionAligned = Math::AlignUp<uint32>(hitSection, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
		totalSize = Math::AlignUp<uint32>(rayGenSectionAligned + missSectionAligned + hitSectionAligned, 256);
		DynamicAllocation allocation = context.AllocateTransientMemory(totalSize);
		char* pStart = (char*)allocation.pMappedMemory;
		char* pData = pStart;
		for (const TableEntry& e : m_RayGenTable)
		{
			memcpy(pData, e.pIdentifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
			memcpy(pData + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, e.data.data(), e.data.size() * sizeof(uint64));
			pData += m_RayGenEntrySize;
		}
		pData = pStart + rayGenSectionAligned;
		for (const TableEntry& e : m_MissTable)
		{
			memcpy(pData, e.pIdentifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
			memcpy(pData + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, e.data.data(), e.data.size() * sizeof(uint64));
			pData += m_RayGenEntrySize;
		}
		pData = pStart + rayGenSectionAligned + missSectionAligned;
		for (const TableEntry& e : m_HitTable)
		{
			memcpy(pData, e.pIdentifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
			memcpy(pData + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, e.data.data(), e.data.size() * sizeof(uint64));
			pData += m_RayGenEntrySize;
		}
		desc.RayGenerationShaderRecord.StartAddress = allocation.GpuHandle;
		desc.RayGenerationShaderRecord.SizeInBytes = rayGenSection;
		desc.MissShaderTable.StartAddress = allocation.GpuHandle + rayGenSectionAligned;
		desc.MissShaderTable.SizeInBytes = missSection;
		desc.MissShaderTable.StrideInBytes = m_MissEntrySize;
		desc.HitGroupTable.StartAddress = allocation.GpuHandle + rayGenSectionAligned + missSectionAligned;
		desc.HitGroupTable.SizeInBytes = hitSection;
		desc.HitGroupTable.StrideInBytes = m_HitEntrySize;

		m_RayGenTable.clear();
		m_RayGenEntrySize = 0;
		m_MissTable.clear();
		m_MissEntrySize = 0;
		m_HitTable.clear();
		m_HitEntrySize = 0;
	}

private:
	TableEntry CreateEntry(const char* pName, const std::vector<void*>& data)
	{
		TableEntry entry;
		auto it = m_IdentifierMap.find(pName);
		if (it == m_IdentifierMap.end())
		{
			wchar_t wName[256];
			ToWidechar(pName, wName, 256);
			m_IdentifierMap[pName] = m_pObjectProperties->GetShaderIdentifier(wName);
		}
		entry.pIdentifier = m_IdentifierMap[pName];
		check(entry.pIdentifier);
		entry.data = data;
		return entry;
	}

	ComPtr<ID3D12StateObjectProperties> m_pObjectProperties;
	std::vector<TableEntry> m_RayGenTable;
	uint32 m_RayGenEntrySize = 0;
	std::vector<TableEntry> m_MissTable;
	uint32 m_MissEntrySize = 0;
	std::vector<TableEntry> m_HitTable;
	uint32 m_HitEntrySize = 0;
	std::unordered_map<std::string, void*> m_IdentifierMap;
};
