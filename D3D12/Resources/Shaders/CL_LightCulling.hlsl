#include "Common.hlsl"

#define MAX_LIGHTS_PER_TILE 256
#define THREAD_COUNT 1024

cbuffer ShaderParameters : register(b0)
{
	float4x4 cView;
}

StructuredBuffer<Light> Lights : register(t0);
StructuredBuffer<AABB> tClusterAABBs : register(t1);
StructuredBuffer<uint> tActiveClusterIndices : register(t2);

globallycoherent RWStructuredBuffer<uint> uLightIndexCounter : register(u0);
RWStructuredBuffer<uint> uLightIndexList : register(u1);
RWStructuredBuffer<uint2> uOutLightGrid : register(u2);

groupshared AABB GroupAABB;
groupshared uint ClusterIndex;

groupshared uint IndexStartOffset;
groupshared uint LightCount;
groupshared uint LightList[MAX_LIGHTS_PER_TILE];

void AddLight(uint lightIndex)
{
	uint index;
	InterlockedAdd(LightCount, 1, index);
	if (index < MAX_LIGHTS_PER_TILE)
	{
		LightList[index] = lightIndex;
	}
}

struct CS_INPUT
{
	uint3 GroupId : SV_GROUPID;
	uint3 GroupThreadId : SV_GROUPTHREADID;
	uint3 DispatchThreadId : SV_DISPATCHTHREADID;
	uint GroupIndex : SV_GROUPINDEX;
};

[numthreads(THREAD_COUNT, 1, 1)]
void LightCulling(CS_INPUT input)
{
	//Initialize the groupshared data only on the first thread of the group
	if (input.GroupIndex == 0)
	{
		LightCount = 0;
		ClusterIndex = tActiveClusterIndices[input.GroupId.x];
		GroupAABB = tClusterAABBs[ClusterIndex];
	}

	//Wait for all the threads to finish
	GroupMemoryBarrierWithGroupSync();

	//Perform the light culling
	for (uint i = input.GroupIndex; i < LIGHT_COUNT; i += THREAD_COUNT)
	{
		Light light = Lights[i];

		switch (light.Type)
		{
		case LIGHT_POINT:
		{
			Sphere sphere = (Sphere)0;
			sphere.Radius = light.Range;
			sphere.Position = mul(float4(light.Position, 1.0f), cView).xyz;
			if (SphereInAABB(sphere, GroupAABB))
			{
				AddLight(i);
			}
		}
		break;
		case LIGHT_SPOT:
		{
			Sphere sphere;
			sphere.Radius = light.Range * 0.5f / pow(cos(radians(light.SpotLightAngle / 2)), 2);
			sphere.Position = mul(float4(light.Position, 1), cView).xyz + mul(light.Direction, (float3x3)cView) * sphere.Radius;
			if (SphereInAABB(sphere, GroupAABB))
			{
				AddLight(i);
			}
		}
		break;
		case LIGHT_DIRECTIONAL:
		{
			AddLight(i);
		}
		break;
		}
	}

	GroupMemoryBarrierWithGroupSync();

	//Populate the light grid only on the first thread in the group
	if (input.GroupIndex == 0)
	{
		InterlockedAdd(uLightIndexCounter[0], LightCount, IndexStartOffset);
		uOutLightGrid[ClusterIndex] = uint2(IndexStartOffset, LightCount);
	}

	GroupMemoryBarrierWithGroupSync();

	//Distribute populating the light index light amonst threads in the thread group
	for (i = input.GroupIndex; i < LightCount; i += THREAD_COUNT)
	{
		uLightIndexList[IndexStartOffset + i] = LightList[i];
	}
}