#include "CommonBindings.hlsli"

#define RootSig ROOT_SIG("CBV(b0), " \
				"DescriptorTable(UAV(u0, numDescriptors = 1))")

struct PassParameters
{
	float4x4 ProjectionInverse;
	float2 ScreenDimensionsInv;
	int2 ClusterSize;
	int3 ClusterDimensions;
	float NearZ;
	float FarZ;
};

RWStructuredBuffer<AABB> uOutAABBs : register(u0);
ConstantBuffer<PassParameters> cPassParameters : register(b0);

float GetDepthFromSlice(uint slice)
{
	return cPassParameters.NearZ * pow(cPassParameters.FarZ / cPassParameters.NearZ, (float)slice / cPassParameters.ClusterDimensions.z);
}

float3 LineFromOriginZIntersection(float3 lineFromOrigin, float depth)
{
	float3 normal = float3(0.0f, 0.0f, 1.0f);
	float t = depth / dot(normal, lineFromOrigin);
	return t * lineFromOrigin;
}

[RootSignature(RootSig)]
[numthreads(1, 1, 32)]
void GenerateAABBs(uint threadID : SV_DispatchThreadID)
{
	uint3 clusterIndex3D = threadID;
	if(clusterIndex3D.z >= cPassParameters.ClusterDimensions.z)
	{
		return;
	}
	uint clusterIndex1D = clusterIndex3D.x + (clusterIndex3D.y * cPassParameters.ClusterDimensions.x) + (clusterIndex3D.z * (cPassParameters.ClusterDimensions.x * cPassParameters.ClusterDimensions.y));

	float2 minPoint_SS = float2(clusterIndex3D.x * cPassParameters.ClusterSize.x, clusterIndex3D.y * cPassParameters.ClusterSize.y);
	float2 maxPoint_SS = float2((clusterIndex3D.x + 1) * cPassParameters.ClusterSize.x, (clusterIndex3D.y + 1) * cPassParameters.ClusterSize.y);

	float3 minPoint_VS = ScreenToView(float4(minPoint_SS, 0, 1), cPassParameters.ScreenDimensionsInv, cPassParameters.ProjectionInverse).xyz;
	float3 maxPoint_VS = ScreenToView(float4(maxPoint_SS, 0, 1), cPassParameters.ScreenDimensionsInv, cPassParameters.ProjectionInverse).xyz;

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
