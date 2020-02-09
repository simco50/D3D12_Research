#include "Common.hlsl"

#define RootSig "CBV(b0, visibility=SHADER_VISIBILITY_ALL), " \
				"DescriptorTable(SRV(t0, numDescriptors = 3)), " \
				"DescriptorTable(UAV(u0, numDescriptors = 3))"

#define MAX_LIGHTS_PER_TILE 256
#define THREAD_COUNT 512

cbuffer ShaderParameters : register(b0)
{
	float4x4 cView;
	uint cLightCount;
}

StructuredBuffer<Light> Lights : register(t0);
StructuredBuffer<AABB> tClusterAABBs : register(t1);
StructuredBuffer<uint> tActiveClusterIndices : register(t2);

globallycoherent RWStructuredBuffer<uint> uLightIndexCounter : register(u0);
RWStructuredBuffer<uint> uLightIndexList : register(u1);
RWStructuredBuffer<uint2> uOutLightGrid : register(u2);

groupshared AABB GroupAABB;
groupshared uint ClusterIndex;

groupshared uint gIndexStartOffset;
groupshared uint gLightCount;
groupshared uint gLightList[MAX_LIGHTS_PER_TILE];

void AddLight(uint lightIndex)
{
	uint index;
	InterlockedAdd(gLightCount, 1, index);
	if (index < MAX_LIGHTS_PER_TILE)
	{
		gLightList[index] = lightIndex;
	}
}

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
	uint3 GroupId : SV_GROUPID;
	uint3 GroupThreadId : SV_GROUPTHREADID;
	uint3 DispatchThreadId : SV_DISPATCHTHREADID;
	uint GroupIndex : SV_GROUPINDEX;
};

[RootSignature(RootSig)]
[numthreads(THREAD_COUNT, 1, 1)]
void LightCulling(CS_INPUT input)
{
	//Initialize the groupshared data only on the first thread of the group
	if (input.GroupIndex == 0)
	{
		gLightCount = 0;
		ClusterIndex = tActiveClusterIndices[input.GroupId.x];
		GroupAABB = tClusterAABBs[ClusterIndex];
	}

	//Wait for all the threads to finish
	GroupMemoryBarrierWithGroupSync();

	//Perform the light culling
	[loop]
	for (uint i = input.GroupIndex; i < cLightCount; i += THREAD_COUNT)
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
#ifdef OPTIMIZED_SPOT_LIGHT_CULLING
			Sphere sphere;
			sphere.Position = GroupAABB.Center;
			sphere.Radius = sqrt(dot(GroupAABB.Extents, GroupAABB.Extents));

			if(ConeInSphere(mul(float4(light.Position, 1), cView).xyz, mul(light.Direction, (float3x3)cView), light.Range, float2(sin(radians(light.SpotLightAngle / 2)), cos(radians(light.SpotLightAngle / 2))), sphere))
			{
				AddLight(i);
			}
#else
			Sphere sphere;
			sphere.Radius = light.Range * 0.5f / pow(cos(radians(light.SpotLightAngle / 2)), 2);
			sphere.Position = mul(float4(light.Position, 1), cView).xyz + mul(light.Direction, (float3x3)cView) * sphere.Radius;
			if (SphereInAABB(sphere, GroupAABB))
			{
				AddLight(i);
			}
#endif
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
		InterlockedAdd(uLightIndexCounter[0], gLightCount, gIndexStartOffset);
		uOutLightGrid[ClusterIndex] = uint2(gIndexStartOffset, gLightCount);
	}

	GroupMemoryBarrierWithGroupSync();

	//Distribute populating the light index light amonst threads in the thread group
	for (i = input.GroupIndex; i < gLightCount; i += THREAD_COUNT)
	{
		uLightIndexList[gIndexStartOffset + i] = gLightList[i];
	}
}