#include "Common.hlsli"
#include "Constants.hlsli"

#define SPLITZ_CULLING 1

struct Plane
{
	float3 Normal;
	float DistanceToOrigin;
};

struct Frustum
{
	Plane Planes[4];
};

struct Cone
{
	float3 Tip;
	float Height;
	float3 Direction;
	float Radius;
};

struct PrecomputedLightData
{
	float3 SphereViewPosition;
	float SphereRadius;
	uint Index;
};

Texture2D tDepthTexture : register(t0);
StructuredBuffer<PrecomputedLightData> tLightData : register(t1);

RWBuffer<uint> uLightListOpaque 		: register(u0);
RWBuffer<uint> uLightListTransparent 	: register(u1);

groupshared uint 	gsMinDepth;
groupshared uint 	gsMaxDepth;
groupshared Frustum gsGroupFrustum;
groupshared AABB 	gsGroupAABB;

groupshared uint 	gsLightBucketsOpaque[TILED_LIGHTING_NUM_BUCKETS];
groupshared uint 	gsLightBucketsTransparent[TILED_LIGHTING_NUM_BUCKETS];

#if SPLITZ_CULLING
groupshared uint 	gsDepthMask;
#endif

bool SphereBehindPlane(Sphere sphere, Plane plane)
{
	return dot(plane.Normal, sphere.Position) - plane.DistanceToOrigin < -sphere.Radius;
}

bool PointBehindPlane(float3 p, Plane plane)
{
	return dot(plane.Normal, p) - plane.DistanceToOrigin < 0;
}

bool SphereInFrustum(Sphere sphere, Frustum frustum, float depthNear, float depthFar)
{
	bool inside = !(sphere.Position.z + sphere.Radius < depthNear || sphere.Position.z - sphere.Radius > depthFar);
	for(int i = 0; i < 4 && inside; ++i)
	{
		inside = !SphereBehindPlane(sphere, frustum.Planes[i]);
	}
	return inside;
}

Plane CalculatePlane(float3 a, float3 b, float3 c)
{
	float3 v0 = b - a;
	float3 v1 = c - a;

	Plane plane;
	plane.Normal = normalize(cross(v1, v0));
	plane.DistanceToOrigin = dot(plane.Normal, a);
	return plane;
}

uint CreateLightMask(float depthRangeMin, float depthRange, Sphere sphere)
{
	float fMin = sphere.Position.z - sphere.Radius;
	float fMax = sphere.Position.z + sphere.Radius;
	uint maskIndexStart = max(0, min(31, floor((fMin - depthRangeMin) * depthRange)));
	uint maskIndexEnd = max(0, min(31, floor((fMax - depthRangeMin) * depthRange)));

	uint mask = 0xFFFFFFFF;
	mask >>= 31 - (maskIndexEnd - maskIndexStart);
	mask <<= maskIndexStart;
	return mask;
}

void AddLightOpaque(uint lightIndex)
{
	uint bucketIndex = lightIndex / 32;
	uint localIndex = lightIndex % 32;
	InterlockedOr(gsLightBucketsOpaque[bucketIndex], 1u << localIndex);
}

void AddLightTransparent(uint lightIndex)
{
	uint bucketIndex = lightIndex / 32;
	uint localIndex = lightIndex % 32;
	InterlockedOr(gsLightBucketsTransparent[bucketIndex], 1u << localIndex);
}

[numthreads(TILED_LIGHTING_TILE_SIZE, TILED_LIGHTING_TILE_SIZE, 1)]
void CSMain(uint3 groupId : SV_GroupID, uint3 threadID : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex)
{
	if(any(threadID.xy >= cView.TargetDimensions.xy))
		return;

	int2 uv = threadID.xy;
	float fDepth = tDepthTexture[uv].r;

	//Convert to uint because you can't used interlocked functions on floats
	uint depth = asuint(fDepth);

	//Initialize the groupshared data only on the first thread of the group
	if (groupIndex == 0)
	{
		gsMinDepth = 0xffffffff;
		gsMaxDepth = 0;
#if SPLITZ_CULLING
		gsDepthMask = 0;
#endif
	}

	if(groupIndex < TILED_LIGHTING_NUM_BUCKETS)
	{
		gsLightBucketsOpaque[groupIndex] = 0;
		gsLightBucketsTransparent[groupIndex] = 0;
	}

	//Wait for thread 0 to finish with initializing the groupshared data
	GroupMemoryBarrierWithGroupSync();

	//Find the min and max depth values in the threadgroup
	InterlockedMin(gsMinDepth, depth);
	InterlockedMax(gsMaxDepth, depth);

	//Wait for all the threads to finish
	GroupMemoryBarrierWithGroupSync();

	float fMinDepth = asfloat(gsMaxDepth);
	float fMaxDepth = asfloat(gsMinDepth);

	if(groupIndex == 0)
	{
		float3 viewSpace[8];
		viewSpace[0] = ScreenToView(float4(groupId.xy * TILED_LIGHTING_TILE_SIZE, fMinDepth, 1.0f), cView.TargetDimensionsInv, cView.ProjectionInverse).xyz;
		viewSpace[1] = ScreenToView(float4(float2(groupId.x + 1, groupId.y) * TILED_LIGHTING_TILE_SIZE, fMinDepth, 1.0f), cView.TargetDimensionsInv, cView.ProjectionInverse).xyz;
		viewSpace[2] = ScreenToView(float4(float2(groupId.x, groupId.y + 1) * TILED_LIGHTING_TILE_SIZE, fMinDepth, 1.0f), cView.TargetDimensionsInv, cView.ProjectionInverse).xyz;
		viewSpace[3] = ScreenToView(float4(float2(groupId.x + 1, groupId.y + 1) * TILED_LIGHTING_TILE_SIZE, fMinDepth, 1.0f), cView.TargetDimensionsInv, cView.ProjectionInverse).xyz;
		viewSpace[4] = ScreenToView(float4(groupId.xy * TILED_LIGHTING_TILE_SIZE, fMaxDepth, 1.0f), cView.TargetDimensionsInv, cView.ProjectionInverse).xyz;
		viewSpace[5] = ScreenToView(float4(float2(groupId.x + 1, groupId.y) * TILED_LIGHTING_TILE_SIZE, fMaxDepth, 1.0f), cView.TargetDimensionsInv, cView.ProjectionInverse).xyz;
		viewSpace[6] = ScreenToView(float4(float2(groupId.x, groupId.y + 1) * TILED_LIGHTING_TILE_SIZE, fMaxDepth, 1.0f), cView.TargetDimensionsInv, cView.ProjectionInverse).xyz;
		viewSpace[7] = ScreenToView(float4(float2(groupId.x + 1, groupId.y + 1) * TILED_LIGHTING_TILE_SIZE, fMaxDepth, 1.0f), cView.TargetDimensionsInv, cView.ProjectionInverse).xyz;

		gsGroupFrustum.Planes[0] = CalculatePlane(float3(0, 0, 0), viewSpace[6], viewSpace[4]);
		gsGroupFrustum.Planes[1] = CalculatePlane(float3(0, 0, 0), viewSpace[5], viewSpace[7]);
		gsGroupFrustum.Planes[2] = CalculatePlane(float3(0, 0, 0), viewSpace[4], viewSpace[5]);
		gsGroupFrustum.Planes[3] = CalculatePlane(float3(0, 0, 0), viewSpace[7], viewSpace[6]);

		float3 minAABB = 1000000;
		float3 maxAABB = -1000000;
		[unroll]
		for(uint i = 0; i < 8; ++i)
		{
			minAABB = min(minAABB, viewSpace[i]);
			maxAABB = max(maxAABB, viewSpace[i]);
		}
		gsGroupAABB = AABBFromMinMax(minAABB, maxAABB);
	}

	// Convert depth values to view space.
	float minDepthVS = ScreenToView(float4(0, 0, fMinDepth, 1), cView.TargetDimensionsInv, cView.ProjectionInverse).z;
	float maxDepthVS = ScreenToView(float4(0, 0, fMaxDepth, 1), cView.TargetDimensionsInv, cView.ProjectionInverse).z;
	float nearClipVS = ScreenToView(float4(0, 0, 1, 1), cView.TargetDimensionsInv, cView.ProjectionInverse).z;

#if SPLITZ_CULLING
	float depthVS = ScreenToView(float4(0, 0, fDepth, 1), cView.TargetDimensionsInv, cView.ProjectionInverse).z;
	float depthRange = 31.0f / (maxDepthVS - minDepthVS);
	uint cellIndex = max(0, min(31, floor((depthVS - minDepthVS) * depthRange)));
	InterlockedOr(gsDepthMask, 1u << cellIndex);
#endif

	GroupMemoryBarrierWithGroupSync();

	// Perform the light culling
	[loop]
	for(uint i = groupIndex; i < cView.LightCount; i += TILED_LIGHTING_TILE_SIZE * TILED_LIGHTING_TILE_SIZE)
	{
		PrecomputedLightData lightData = tLightData[i];
		Sphere sphere;
		sphere.Position = lightData.SphereViewPosition;
		sphere.Radius = lightData.SphereRadius;

		if (SphereInFrustum(sphere, gsGroupFrustum, nearClipVS, maxDepthVS))
		{
			AddLightTransparent(lightData.Index);

			if(SphereInAABB(sphere, gsGroupAABB))
			{
#if SPLITZ_CULLING
				if(gsDepthMask & CreateLightMask(minDepthVS, depthRange, sphere))
#endif
				{
					AddLightOpaque(lightData.Index);
				}
			}
		}
	}

	GroupMemoryBarrierWithGroupSync();

	uint tileIndex = groupId.x + DivideAndRoundUp(cView.TargetDimensions.x, TILED_LIGHTING_TILE_SIZE) * groupId.y;
	uint lightGridOffset = tileIndex * TILED_LIGHTING_NUM_BUCKETS;
	if(groupIndex < TILED_LIGHTING_NUM_BUCKETS)
	{
		uLightListTransparent[lightGridOffset + groupIndex] = gsLightBucketsTransparent[groupIndex];
		uLightListOpaque[lightGridOffset + groupIndex] = gsLightBucketsOpaque[groupIndex];
	}
}
