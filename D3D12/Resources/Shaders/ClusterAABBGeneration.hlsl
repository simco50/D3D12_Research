#include "CommonBindings.hlsli"

#define RootSig ROOT_SIG("CBV(b0, visibility=SHADER_VISIBILITY_ALL), " \
				"DescriptorTable(UAV(u0, numDescriptors = 1), visibility = SHADER_VISIBILITY_ALL)")

cbuffer Parameters : register(b0)
{
    float4x4 cProjectionInverse;
    float2 cScreenDimensionsInv;
    int2 cClusterSize;
    int3 cClusterDimensions;
    float cNearZ;
    float cFarZ;
}

float GetDepthFromSlice(uint slice)
{
    return cNearZ * pow(cFarZ / cNearZ, (float)slice / cClusterDimensions.z);
}

float3 LineFromOriginZIntersection(float3 lineFromOrigin, float depth)
{
    float3 normal = float3(0.0f, 0.0f, 1.0f);
    float t = depth / dot(normal, lineFromOrigin);
    return t * lineFromOrigin;
}

RWStructuredBuffer<AABB> uOutAABBs : register(u0);

struct CS_Input
{
    uint3 ThreadID : SV_DISPATCHTHREADID;
};

[RootSignature(RootSig)]
[numthreads(1, 1, 32)]
void GenerateAABBs(CS_Input input)
{   
    uint3 clusterIndex3D = input.ThreadID;
    if(clusterIndex3D.z >= cClusterDimensions.z)
    {
        return;
    }
    uint clusterIndex1D = clusterIndex3D.x + (clusterIndex3D.y * cClusterDimensions.x) + (clusterIndex3D.z * (cClusterDimensions.x * cClusterDimensions.y));

    float2 minPoint_SS = float2(clusterIndex3D.x * cClusterSize.x, clusterIndex3D.y * cClusterSize.y);
    float2 maxPoint_SS = float2((clusterIndex3D.x + 1) * cClusterSize.x, (clusterIndex3D.y + 1) * cClusterSize.y);

    float3 minPoint_VS = ScreenToView(float4(minPoint_SS, 0, 1), cScreenDimensionsInv, cProjectionInverse).xyz;
    float3 maxPoint_VS = ScreenToView(float4(maxPoint_SS, 0, 1), cScreenDimensionsInv, cProjectionInverse).xyz;

    float farZ = GetDepthFromSlice(clusterIndex3D.z);
    float nearZ = GetDepthFromSlice(clusterIndex3D.z + 1);

    float3 minPointNear = LineFromOriginZIntersection(minPoint_VS, nearZ);
    float3 maxPointNear = LineFromOriginZIntersection(maxPoint_VS, nearZ);
    float3 minPointFar = LineFromOriginZIntersection(minPoint_VS, farZ);
    float3 maxPointFar = LineFromOriginZIntersection(maxPoint_VS, farZ);

    float3 bbMin = min(min(minPointNear, minPointFar), min(maxPointNear, maxPointFar));
    float3 bbMax = max(max(minPointNear, minPointFar), max(maxPointNear, maxPointFar));

    AABBFromMinMax(uOutAABBs[clusterIndex1D], bbMin, bbMax);
}