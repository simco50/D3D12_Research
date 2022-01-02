#include "CommonBindings.hlsli"

#define RootSig ROOT_SIG("CBV(b0), " \
				"CBV(b100)," \
				"DescriptorTable(UAV(u0, numDescriptors = 1))")

struct PassParameters
{
	int4 ClusterDimensions;
	int2 ClusterSize;
};

RWStructuredBuffer<AABB> uOutAABBs : register(u0);
ConstantBuffer<PassParameters> cPass : register(b0);

float GetDepthFromSlice(uint slice)
{
	return cView.FarZ * pow(cView.NearZ / cView.FarZ, (float)slice / cPass.ClusterDimensions.z);
}

float3 LineFromOriginZIntersection(float3 lineFromOrigin, float depth)
{
	float3 normal = float3(0.0f, 0.0f, 1.0f);
	float t = depth / dot(normal, lineFromOrigin);
	return t * lineFromOrigin;
}

[RootSignature(RootSig)]
[numthreads(1, 1, 32)]
void GenerateAABBs(uint3 threadID : SV_DispatchThreadID)
{
	uint3 clusterIndex3D = threadID;
	if(clusterIndex3D.z >= cPass.ClusterDimensions.z)
	{
		return;
	}
	uint clusterIndex1D = clusterIndex3D.x + (clusterIndex3D.y * cPass.ClusterDimensions.x) + (clusterIndex3D.z * (cPass.ClusterDimensions.x * cPass.ClusterDimensions.y));

	float2 minPoint_SS = float2(clusterIndex3D.x * cPass.ClusterSize.x, clusterIndex3D.y * cPass.ClusterSize.y);
	float2 maxPoint_SS = float2((clusterIndex3D.x + 1) * cPass.ClusterSize.x, (clusterIndex3D.y + 1) * cPass.ClusterSize.y);

	float3 minPoint_VS = ScreenToView(float4(minPoint_SS, 0, 1), cView.ScreenDimensionsInv, cView.ProjectionInverse).xyz;
	float3 maxPoint_VS = ScreenToView(float4(maxPoint_SS, 0, 1), cView.ScreenDimensionsInv, cView.ProjectionInverse).xyz;

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
