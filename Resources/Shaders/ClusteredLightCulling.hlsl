#include "Common.hlsli"

#define THREAD_COUNT 4

struct PrecomputedLightData
{
	float3 ViewSpacePosition;
	float SpotCosAngle;
	float3 ViewSpaceDirection;
	float SpotSinAngle;
	float Range;
	uint IsSpot : 1;
	uint IsPoint : 1;
	uint IsDirectional : 1;
};

struct PassParams
{
	int4 ClusterDimensions;
	int2 ClusterSize;
	RWTypedBufferH<uint> LightGrid;
	StructuredBufferH<PrecomputedLightData> LightData;
};
DEFINE_CONSTANTS(PassParams, 0);

bool ConeInSphere(float3 conePosition, float3 coneDirection, float coneRange, float2 coneAngleSinCos, Sphere sphere)
{
	float3 v = sphere.Position - conePosition;
	float lenSq = dot(v, v);
	float v1Len = dot(v, coneDirection);
	float distanceClosestPoint = coneAngleSinCos.y * sqrt(lenSq - v1Len * v1Len) - v1Len * coneAngleSinCos.x;
	bool angleCull = distanceClosestPoint > sphere.Radius;
	bool frontCull = v1Len > sphere.Radius + coneRange;
	bool backCull = v1Len < -sphere.Radius;
	return !(angleCull || frontCull || backCull);
}

float GetDepthFromSlice(uint slice)
{
	return cView.FarZ * pow(cView.NearZ / cView.FarZ, (float)slice / cPassParams.ClusterDimensions.z);
}

float3 LineFromOriginZIntersection(float3 lineFromOrigin, float depth)
{
	float3 normal = float3(0.0f, 0.0f, 1.0f);
	float t = depth / dot(normal, lineFromOrigin);
	return t * lineFromOrigin;
}

AABB ComputeAABB(uint3 clusterIndex3D)
{
	float2 minPoint_SS = float2(clusterIndex3D.x * cPassParams.ClusterSize.x, clusterIndex3D.y * cPassParams.ClusterSize.y);
	float2 maxPoint_SS = float2((clusterIndex3D.x + 1) * cPassParams.ClusterSize.x, (clusterIndex3D.y + 1) * cPassParams.ClusterSize.y);

	float3 minPoint_VS = ScreenToView(float4(minPoint_SS, 0, 1), cView.ViewportDimensionsInv, cView.ClipToView).xyz;
	float3 maxPoint_VS = ScreenToView(float4(maxPoint_SS, 0, 1), cView.ViewportDimensionsInv, cView.ClipToView).xyz;

	float farZ = GetDepthFromSlice(clusterIndex3D.z);
	float nearZ = GetDepthFromSlice(clusterIndex3D.z + 1);

	float3 minPointNear = LineFromOriginZIntersection(minPoint_VS, nearZ);
	float3 maxPointNear = LineFromOriginZIntersection(maxPoint_VS, nearZ);
	float3 minPointFar = LineFromOriginZIntersection(minPoint_VS, farZ);
	float3 maxPointFar = LineFromOriginZIntersection(maxPoint_VS, farZ);

	float3 bbMin = min(min(minPointNear, minPointFar), min(maxPointNear, maxPointFar));
	float3 bbMax = max(max(minPointNear, minPointFar), max(maxPointNear, maxPointFar));

	return AABBFromMinMax(bbMin, bbMax);
}

[numthreads(THREAD_COUNT, THREAD_COUNT, THREAD_COUNT)]
void LightCulling(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	uint3 clusterIndex3D = dispatchThreadId;
	if(any(clusterIndex3D >= cPassParams.ClusterDimensions.xyz))
		return;

	AABB clusterAABB = ComputeAABB(clusterIndex3D);
	float clusterRadius = sqrt(dot(clusterAABB.Extents.xyz, clusterAABB.Extents.xyz));

	uint lightIndex = 0;

	[loop]
	for(uint bucketIndex = 0; bucketIndex < CLUSTERED_LIGHTING_NUM_BUCKETS && lightIndex < cView.LightCount; ++bucketIndex)
	{
		uint lightMask = 0;

		[loop]
		for(uint i = 0; i < 32 && lightIndex < cView.LightCount; ++i)
		{
			PrecomputedLightData lightData = cPassParams.LightData[lightIndex];
			++lightIndex;

			if(lightData.IsPoint)
			{
				Sphere sphere;
				sphere.Radius = lightData.Range;
				sphere.Position = lightData.ViewSpacePosition;
				if (SphereInAABB(sphere, clusterAABB))
				{
					lightMask |= 1u << i;
				}
			}
			else if(lightData.IsSpot)
			{
				Sphere sphere;
				sphere.Radius = clusterRadius;
				sphere.Position = clusterAABB.Center.xyz;

				if (ConeInSphere(
					lightData.ViewSpacePosition,
					lightData.ViewSpaceDirection,
					lightData.Range,
					float2(lightData.SpotSinAngle, lightData.SpotCosAngle),
					sphere))
				{
					lightMask |= 1u << i;
				}
			}
			else
			{
				lightMask |= 1u << i;
			}
		}

		uint clusterIndex = Flatten3D(dispatchThreadId, cPassParams.ClusterDimensions.xy);
		cPassParams.LightGrid.Store(clusterIndex * CLUSTERED_LIGHTING_NUM_BUCKETS + bucketIndex, lightMask);
	}
}
