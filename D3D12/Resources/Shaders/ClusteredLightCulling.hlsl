#include "Common.hlsli"

#define RootSig "CBV(b0, visibility=SHADER_VISIBILITY_ALL), " \
				"DescriptorTable(SRV(t0, numDescriptors = 2)), " \
				"DescriptorTable(UAV(u0, numDescriptors = 3))"

#define MAX_LIGHTS_PER_TILE 256
#define THREAD_COUNT 4

cbuffer ShaderParameters : register(b0)
{
	float4x4 cView;
	int3 cClusterDimensions;
	uint cLightCount;
}

StructuredBuffer<Light> tLights : register(t0);
StructuredBuffer<AABB> tClusterAABBs : register(t1);

globallycoherent RWStructuredBuffer<uint> uLightIndexCounter : register(u0);
RWStructuredBuffer<uint> uLightIndexList : register(u1);
RWStructuredBuffer<uint2> uOutLightGrid : register(u2);

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

struct CS_INPUT
{
	uint3 DispatchThreadId : SV_DISPATCHTHREADID;
};

[RootSignature(RootSig)]
[numthreads(THREAD_COUNT, THREAD_COUNT, THREAD_COUNT)]
void LightCulling(CS_INPUT input)
{
	uint lightList[MAX_LIGHTS_PER_TILE];
	uint lightCount = 0;
	uint clusterIndex = input.DispatchThreadId.x + input.DispatchThreadId.y * cClusterDimensions.x + input.DispatchThreadId.z * cClusterDimensions.x * cClusterDimensions.y;
	AABB clusterAABB = tClusterAABBs[clusterIndex];

	//Perform the light culling
	[loop]
	for (uint i = 0; i < cLightCount; ++i)
	{
		Light light = tLights[i];
		if(light.IsPoint())
		{
			Sphere sphere = (Sphere)0;
			sphere.Radius = light.Range;
			sphere.Position = mul(float4(light.Position, 1.0f), cView).xyz;
			if (SphereInAABB(sphere, clusterAABB))
			{
				uint index = lightCount;
				lightCount++;
				if (index < MAX_LIGHTS_PER_TILE)
				{
					lightList[index] = i;
				}
			}
		}
		else if(light.IsSpot())
		{
			Sphere sphere;
			sphere.Radius = sqrt(dot(clusterAABB.Extents.xyz, clusterAABB.Extents.xyz));
			sphere.Position = clusterAABB.Center.xyz;

			float3 conePosition = mul(float4(light.Position, 1), cView).xyz;
			float3 coneDirection = mul(light.Direction, (float3x3)cView);
			float angle = acos(light.SpotlightAngles.y);
			if (ConeInSphere(conePosition, coneDirection, light.Range, float2(sin(angle), light.SpotlightAngles.y), sphere))
			{
				uint index = lightCount;
				lightCount++;
				if (index < MAX_LIGHTS_PER_TILE)
				{
					lightList[index] = i;
				}
			}
		}
		else
		{
			uint index = lightCount;
			lightCount++;
			if (index < MAX_LIGHTS_PER_TILE)
			{
				lightList[index] = i;
			}
		}
	}

	//Populate the light grid only on the first thread in the group
	uint startOffset = 0;
	InterlockedAdd(uLightIndexCounter[0], lightCount, startOffset);
	uOutLightGrid[clusterIndex] = uint2(startOffset, lightCount);

	//Distribute populating the light index light amonst threads in the thread group
	for (i = 0; i < lightCount; i++)
	{
		uLightIndexList[startOffset + i] = lightList[i];
	}
}