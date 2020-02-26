#include "Common.hlsli"

#define MAX_LIGHTS_PER_TILE 32
#define THREAD_GROUP_SIZE 4

cbuffer ShaderParameters : register(b0)
{
	float4x4 cView;
	uint3 cClusterDimensions;
	uint cLightCount;
}

StructuredBuffer<Light> Lights : register(t0);
StructuredBuffer<AABB> tClusterAABBs : register(t1);
StructuredBuffer<uint> tActiveClusterIndices : register(t2);

globallycoherent RWStructuredBuffer<uint> uLightIndexCounter : register(u0);
RWStructuredBuffer<uint> uLightIndexList : register(u1);
RWStructuredBuffer<uint2> uOutLightGrid : register(u2);

void AddLight(uint clusterIndex, uint lightIndex)
{
	uint index;
	InterlockedAdd(uOutLightGrid[clusterIndex].y, 1, index);
	uOutLightGrid[clusterIndex].x = clusterIndex * MAX_LIGHTS_PER_TILE;
	if(index < MAX_LIGHTS_PER_TILE)
	{
		uLightIndexList[clusterIndex * MAX_LIGHTS_PER_TILE + index] = lightIndex;
	}
}

struct CS_INPUT
{
	uint3 GroupId : SV_GROUPID;
	uint3 GroupThreadId : SV_GROUPTHREADID;
	uint3 DispatchThreadId : SV_DISPATCHTHREADID;
	uint GroupIndex : SV_GROUPINDEX;
};

[numthreads(THREAD_GROUP_SIZE, THREAD_GROUP_SIZE, THREAD_GROUP_SIZE)]
void LightCulling(CS_INPUT input)
{
	uint3 gridCoordinate = input.DispatchThreadId;
	if (all(gridCoordinate < cClusterDimensions))
	{
		uint clusterIndex = (gridCoordinate.z * cClusterDimensions.y + gridCoordinate.y) * cClusterDimensions.x + gridCoordinate.x;
		AABB aabb = tClusterAABBs[clusterIndex];

		//Perform the light culling
		for (uint i = 0; i < cLightCount; ++i)
		{
			Light light = Lights[i];

			switch (light.Type)
			{
			case LIGHT_POINT:
			{
				Sphere sphere = (Sphere)0;
				sphere.Radius = light.Range;
				sphere.Position = mul(float4(light.Position, 1.0f), cView).xyz;
				if (SphereInAABB(sphere, aabb))
				{
					AddLight(clusterIndex, i);
				}
			}
			break;
			case LIGHT_SPOT:
			{
				Sphere sphere;
				sphere.Radius = light.Range * 0.5f / pow(light.CosSpotLightAngle, 2);
				sphere.Position = mul(float4(light.Position, 1), cView).xyz + mul(light.Direction, (float3x3)cView) * sphere.Radius;
				if (SphereInAABB(sphere, aabb))
				{
					AddLight(clusterIndex, i);
				}
			}
			break;
			case LIGHT_DIRECTIONAL:
			{
				AddLight(clusterIndex, i);
			}
			break;
			}
		}
	}
}