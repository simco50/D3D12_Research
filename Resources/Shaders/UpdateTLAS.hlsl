#include "Common.hlsli"
#include "D3D12.hlsli"

struct BLASInstance
{
	D3D12_GPU_VIRTUAL_ADDRESS AccelerationStructure;
	uint InstanceID;
	uint Flags				: 8;
	uint InstanceMask		: 8;
};

struct PassParams
{
	uint NumInstances;
	StructuredBufferH<InstanceData> InstanceData;
	StructuredBufferH<BLASInstance> InputInstances;
	RWStructuredBufferH<D3D12_RAYTRACING_INSTANCE_DESC> OutputInstances;
};

DEFINE_CONSTANTS(PassParams, 0);

[numthreads(32, 1, 1)]
void UpdateTLASCS(uint threadID : SV_DispatchThreadID)
{
	if(threadID < cPassParams.NumInstances)
	{
		BLASInstance blasDesc = cPassParams.InputInstances[threadID];
		D3D12_RAYTRACING_INSTANCE_DESC output;
		output.InstanceID = blasDesc.InstanceID;
		output.InstanceMask = blasDesc.InstanceMask;
		output.InstanceContributionToHitGroupIndex = 0;
		output.Flags = blasDesc.Flags;
		output.AccelerationStructure = blasDesc.AccelerationStructure;
		InstanceData instance = cPassParams.InstanceData[blasDesc.InstanceID];
		output.Transform = (float3x4)transpose(instance.LocalToWorld);
		cPassParams.OutputInstances.Store(threadID, output);
	}
}
