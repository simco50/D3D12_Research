#pragma once

// Should reflect d3d12.h
struct D3D12_UINT64
{
	uint LowPart;
	uint HighPart;
};

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

struct D3D12_DISPATCH_ARGUMENTS
{
	uint3 ThreadGroupCount;
};

struct D3D12_DRAW_ARGUMENTS
{
	uint VertexCountPerInstance;
	uint InstanceCount;
	uint StartVertexLocation;
	uint StartInstanceLocation;
};

struct D3D12_DRAW_INDEXED_ARGUMENTS
{
	uint IndexCountPerInstance;
	uint InstanceCount;
	uint StartIndexLocation;
	int BaseVertexLocation;
	uint StartInstanceLocation;
};

struct D3D12_GPU_VIRTUAL_ADDRESS_AND_STRIDE
{
    D3D12_GPU_VIRTUAL_ADDRESS StartAddress;
    D3D12_UINT64 StrideInBytes;
};

struct D3D12_NODE_GPU_INPUT
{
    uint EntrypointIndex;
    uint NumRecords;
    D3D12_GPU_VIRTUAL_ADDRESS_AND_STRIDE Records;
};

struct D3D12_MULTI_NODE_GPU_INPUT
{
    uint NumNodeInputs;
	uint padding;
    D3D12_GPU_VIRTUAL_ADDRESS_AND_STRIDE NodeInputs;
};
