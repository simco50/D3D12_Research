#pragma once

// Should reflect d3d12.h

struct D3D12_GPU_VIRTUAL_ADDRESS
{
	uint LowPart;
	uint HighPart;
};

struct D3D12_RAYTRACING_INSTANCE_DESC
{
	float3x4 Transform;
    uint InstanceID	: 24;
    uint InstanceMask : 8;
    uint InstanceContributionToHitGroupIndex : 24;
    uint Flags : 8;
    D3D12_GPU_VIRTUAL_ADDRESS AccelerationStructure;
};
