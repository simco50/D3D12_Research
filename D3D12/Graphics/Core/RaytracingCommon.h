#pragma once

class ShaderBindingTable
{
private:
	struct ShaderRecord
	{
		std::vector<uint64> data;
		const void* pIdentifier = nullptr;
	};
public:
	ShaderBindingTable(ID3D12StateObject* pStateObject)
	{
		VERIFY_HR(pStateObject->QueryInterface(IID_PPV_ARGS(m_pObjectProperties.GetAddressOf())));
	}

	void BindRayGenShader(const char* pName, const std::vector<uint64>& data = {})
	{
		m_RayGenRecord = CreateRecord(pName, data);
		m_RayGenRecordSize = ComputeRecordSize((uint32)data.size());
	}

	void BindMissShader(const char* pName, uint32 rayIndex, const std::vector<uint64>& data = {})
	{
		if (rayIndex >= (uint32)m_MissShaderRecords.size())
		{
			m_MissShaderRecords.resize(rayIndex + 1);
		}
		m_MissShaderRecords[rayIndex] = CreateRecord(pName, data);

		uint32 entrySize = Math::AlignUp<uint32>(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + (uint32)data.size() * sizeof(uint64), D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
		m_MissRecordSize = Math::Max<int>(m_MissRecordSize, entrySize);
	}

	void BindHitGroup(const char* pName, const std::vector<uint64>& data = {})
	{
		m_HitGroupShaderRecords.push_back(CreateRecord(pName, data));
		uint32 entrySize = Math::AlignUp<uint32>(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + (uint32)data.size() * sizeof(uint64), D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
		m_HitRecordSize = Math::Max<int>(m_HitRecordSize, entrySize);
	}

	void Commit(CommandContext& context, D3D12_DISPATCH_RAYS_DESC& desc)
	{
		uint32 totalSize = 0;
		uint32 rayGenSection = m_RayGenRecordSize;
		uint32 rayGenSectionAligned = Math::AlignUp<uint32>(rayGenSection, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
		uint32 missSection = m_MissRecordSize * (uint32)m_MissShaderRecords.size();
		uint32 missSectionAligned = Math::AlignUp<uint32>(missSection, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
		uint32 hitSection = m_HitRecordSize * (uint32)m_HitGroupShaderRecords.size();
		uint32 hitSectionAligned = Math::AlignUp<uint32>(hitSection, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
		totalSize = Math::AlignUp<uint32>(rayGenSectionAligned + missSectionAligned + hitSectionAligned, 256);
		DynamicAllocation allocation = context.AllocateTransientMemory(totalSize);
		allocation.Clear();

		char* pStart = (char*)allocation.pMappedMemory;
		char* pData = pStart;

		// RayGen
		{
			memcpy(pData, m_RayGenRecord.pIdentifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
			memcpy(pData + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, m_RayGenRecord.data.data(), m_RayGenRecord.data.size() * sizeof(uint64));
			pData += m_RayGenRecordSize;
		}
		pData = pStart + rayGenSectionAligned;

		// Miss
		for (const ShaderRecord& e : m_MissShaderRecords)
		{
			memcpy(pData, e.pIdentifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
			memcpy(pData + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, e.data.data(), e.data.size() * sizeof(uint64));
			pData += m_MissRecordSize;
		}
		pData = pStart + rayGenSectionAligned + missSectionAligned;

		// Hit
		for (const ShaderRecord& e : m_HitGroupShaderRecords)
		{
			memcpy(pData, e.pIdentifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
			memcpy(pData + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, e.data.data(), e.data.size() * sizeof(uint64));
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

private:
	uint32 ComputeRecordSize(uint32 elements)
	{
		return Math::AlignUp<uint32>(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + elements * sizeof(uint64), D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
	}

	ShaderRecord CreateRecord(const char* pName, const std::vector<uint64>& data)
	{
		ShaderRecord entry;
		if (pName)
		{
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
		}
		else
		{
			constexpr const void* NullEntry = (void*)"";
			entry.pIdentifier = NullEntry;
		}
		return entry;
	}
	ComPtr<ID3D12StateObjectProperties> m_pObjectProperties;
	ShaderRecord m_RayGenRecord;
	uint32 m_RayGenRecordSize = 0;
	std::vector<ShaderRecord> m_MissShaderRecords;
	uint32 m_MissRecordSize = 0;
	std::vector<ShaderRecord> m_HitGroupShaderRecords;
	uint32 m_HitRecordSize = 0;
	std::unordered_map<std::string, const void*> m_IdentifierMap;
};
