#pragma once

class StateObject;
class CommandContext;

class ShaderBindingTable
{
private:
	struct ShaderRecord
	{
		std::unique_ptr<char[]> pData;
		uint32 Size = 0;
		const void* pIdentifier = nullptr;
	};
public:
	ShaderBindingTable(StateObject* pStateObject);
	void BindRayGenShader(const char* pName, const Span<uint64>& data = {});
	void BindMissShader(const char* pName, uint32 rayIndex, const Span<uint64>& data = {});
	void BindHitGroup(const char* pName, uint32 index, const Span<uint64>& data = {});
	void BindHitGroup(const char* pName, uint32 index, const void* pData, uint32 dataSize);
	template<typename T>
	void BindHitGroup(const char* pName, uint32 index, const T& data)
	{
		BindHitGroup(pName, index, &data, sizeof(T));
	}
	void Commit(CommandContext& context, D3D12_DISPATCH_RAYS_DESC& desc);

private:
	ShaderRecord CreateRecord(const char* pName, const void* pData, uint32 dataSize);
	StateObject* m_pStateObject;
	ShaderRecord m_RayGenRecord;
	uint32 m_RayGenRecordSize = 0;
	std::vector<ShaderRecord> m_MissShaderRecords;
	uint32 m_MissRecordSize = 0;
	std::vector<ShaderRecord> m_HitGroupShaderRecords;
	uint32 m_HitRecordSize = 0;
	std::unordered_map<std::string, const void*> m_IdentifierMap;
};
