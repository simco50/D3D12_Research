#include "Common.hlsli"

#define MAX_LIGHTS_PER_TILE 32
#define THREAD_COUNT 4

struct PassData
{
	int3 ClusterDimensions;
};

ConstantBuffer<PassData> cPass : register(b0);

StructuredBuffer<AABB> tClusterAABBs : register(t0);

RWStructuredBuffer<uint> uLightIndexList : register(u0);
RWStructuredBuffer<uint> uOutLightGrid : register(u1);

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

[numthreads(THREAD_COUNT, THREAD_COUNT, THREAD_COUNT)]
void LightCulling(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	uint3 clusterIndex3D = dispatchThreadId;
	if(any(clusterIndex3D >= cPass.ClusterDimensions))
		return;

	uint clusterIndex = Flatten3D(dispatchThreadId, cPass.ClusterDimensions);
	AABB clusterAABB = tClusterAABBs[clusterIndex];

	uint numLights = 0;

	[loop]
	for (uint i = 0; i < cView.LightCount && numLights < MAX_LIGHTS_PER_TILE; ++i)
	{
		Light light = GetLight(i);
		if(light.IsPoint)
		{
			Sphere sphere = (Sphere)0;
			sphere.Radius = light.Range;
			sphere.Position = mul(float4(light.Position, 1.0f), cView.View).xyz;
			if (SphereInAABB(sphere, clusterAABB))
			{
				uLightIndexList[clusterIndex * MAX_LIGHTS_PER_TILE + numLights] = i;
				++numLights;
			}
		}
		else if(light.IsSpot)
		{
			Sphere sphere;
			sphere.Radius = sqrt(dot(clusterAABB.Extents.xyz, clusterAABB.Extents.xyz));
			sphere.Position = clusterAABB.Center.xyz;

			float3 conePosition = mul(float4(light.Position, 1), cView.View).xyz;
			float3 coneDirection = mul(light.Direction, (float3x3)cView.View);
			float angle = acos(light.SpotlightAngles.y);
			if (ConeInSphere(conePosition, coneDirection, light.Range, float2(sin(angle), light.SpotlightAngles.y), sphere))
			{
				uLightIndexList[clusterIndex * MAX_LIGHTS_PER_TILE + numLights] = i;
				++numLights;
			}
		}
		else
		{
			uLightIndexList[clusterIndex * MAX_LIGHTS_PER_TILE + numLights] = i;
			++numLights;
		}
	}

	uOutLightGrid[clusterIndex * 2] = clusterIndex * MAX_LIGHTS_PER_TILE;
	uOutLightGrid[clusterIndex * 2 + 1] = numLights;
}
