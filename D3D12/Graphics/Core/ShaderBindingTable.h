#pragma once

class StateObject;
class CommandContext;

class ShaderBindingTable
{
private:
	struct ShaderRecord
	{
		std::vector<uint64> data;
		const void* pIdentifier = nullptr;
	};
public:
	ShaderBindingTable(StateObject* pStateObject);
	void BindRayGenShader(const char* pName, const std::vector<uint64>& data = {});
	void BindMissShader(const char* pName, uint32 rayIndex, const std::vector<uint64>& data = {});
	void BindHitGroup(const char* pName, const std::vector<uint64>& data = {});
	void Commit(CommandContext& context, D3D12_DISPATCH_RAYS_DESC& desc);

private:
	uint32 ComputeRecordSize(uint32 elements);

	ShaderRecord CreateRecord(const char* pName, const std::vector<uint64>& data);
	StateObject* m_pStateObject;
	ShaderRecord m_RayGenRecord;
	uint32 m_RayGenRecordSize = 0;
	std::vector<ShaderRecord> m_MissShaderRecords;
	uint32 m_MissRecordSize = 0;
	std::vector<ShaderRecord> m_HitGroupShaderRecords;
	uint32 m_HitRecordSize = 0;
	std::unordered_map<std::string, const void*> m_IdentifierMap;
};
