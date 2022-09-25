#include "Common.hlsli"
#include "D3D12.hlsli"

struct PassParameters
{
	uint NumInstances;
};

struct BLASInstance
{
	D3D12_GPU_VIRTUAL_ADDRESS AccelerationStructure;
	uint MatrixIndex;
	uint Flags;
};

ConstantBuffer<PassParameters> cPass : register(b0);
StructuredBuffer<BLASInstance> tInputInstances : register(t0);
RWStructuredBuffer<D3D12_RAYTRACING_INSTANCE_DESC> uOutputInstances : register(u0);

[numthreads(32, 1, 1)]
void UpdateTLASCS(uint threadID : SV_DispatchThreadID)
{
	if(threadID < cPass.NumInstances)
	{
		BLASInstance instance = tInputInstances[threadID];
		D3D12_RAYTRACING_INSTANCE_DESC output;
		output.InstanceID = instance.MatrixIndex;
		output.InstanceMask = 0xFF;
		output.InstanceContributionToHitGroupIndex = 0;
		output.Flags = instance.Flags;
		output.AccelerationStructure = instance.AccelerationStructure;
		float4x4 world = GetTransform(instance.MatrixIndex);
		output.Transform = (float3x4)transpose(world);
		uOutputInstances[threadID] = output;
	}
}
