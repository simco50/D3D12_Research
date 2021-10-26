#include "CommonBindings.hlsli"

#define RootSig ROOT_SIG("CBV(b0, visibility=SHADER_VISIBILITY_ALL), " \
				"DescriptorTable(SRV(t0, numDescriptors = 2)), " \
				"DescriptorTable(UAV(u0, numDescriptors = 2))")

#define MAX_LIGHTS_PER_TILE 32
#define THREAD_COUNT 4

struct ViewData
{
	float4x4 View;
	int3 ClusterDimensions;
	uint LightCount;
};

ConstantBuffer<ViewData> cViewData : register(b0);

StructuredBuffer<Light> tSceneLights : register(t0);
StructuredBuffer<AABB> tClusterAABBs : register(t1);

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

uint GetClusterIndex1D(uint3 clusterIndex)
{
	return clusterIndex.x + (clusterIndex.y + clusterIndex.z * cViewData.ClusterDimensions.y) * cViewData.ClusterDimensions.x;
}

void AddLight(uint clusterIndex, uint lightIndex)
{
	uint culledLightIndex;
	InterlockedAdd(uOutLightGrid[clusterIndex * 2 + 1], 1, culledLightIndex);
	uOutLightGrid[clusterIndex * 2] = clusterIndex * MAX_LIGHTS_PER_TILE;
	if(culledLightIndex < MAX_LIGHTS_PER_TILE)
	{
		uLightIndexList[clusterIndex * MAX_LIGHTS_PER_TILE + culledLightIndex] = lightIndex;
	}
}

[RootSignature(RootSig)]
[numthreads(THREAD_COUNT, THREAD_COUNT, THREAD_COUNT)]
void LightCulling(uint3 dispatchThreadId : SV_DISPATCHTHREADID)
{
	uint3 clusterIndex3D = dispatchThreadId;

	if(all(clusterIndex3D < cViewData.ClusterDimensions))
	{
		uint clusterIndex = GetClusterIndex1D(dispatchThreadId);
		AABB clusterAABB = tClusterAABBs[clusterIndex];

		//Perform the light culling
		[loop]
		for (uint i = 0; i < cViewData.LightCount; ++i)
		{
			Light light = tSceneLights[i];
			if(light.IsPoint())
			{
				Sphere sphere = (Sphere)0;
				sphere.Radius = light.Range;
				sphere.Position = mul(float4(light.Position, 1.0f), cViewData.View).xyz;
				if (SphereInAABB(sphere, clusterAABB))
				{
					AddLight(clusterIndex, i);
				}
			}
			else if(light.IsSpot())
			{
				Sphere sphere;
				sphere.Radius = sqrt(dot(clusterAABB.Extents.xyz, clusterAABB.Extents.xyz));
				sphere.Position = clusterAABB.Center.xyz;

				float3 conePosition = mul(float4(light.Position, 1), cViewData.View).xyz;
				float3 coneDirection = mul(light.Direction, (float3x3)cViewData.View);
				float angle = acos(light.SpotlightAngles.y);
				if (ConeInSphere(conePosition, coneDirection, light.Range, float2(sin(angle), light.SpotlightAngles.y), sphere))
				{
					AddLight(clusterIndex, i);
				}
			}
			else
			{
				AddLight(clusterIndex, i);
			}
		}
	}
}
