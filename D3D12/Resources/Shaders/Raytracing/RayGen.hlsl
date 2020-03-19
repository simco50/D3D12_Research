#include "Common.hlsl"

#define RPP 64
#define RPP_ACTUAL 1

RWTexture2D<float4> gOutput : register(u0);

RaytracingAccelerationStructure SceneBVH : register(t0);

Texture2D tNormals : register(t1);
Texture2D tDepth : register(t2);
Texture2D tNoise : register(t3);

cbuffer ShaderParameters : register(b0)
{
	float4x4 cViewInverse;
	float4x4 cProjectionInverse;
	float4 cRandomVectors[RPP];
}

[shader("raygeneration")] 
void RayGen() 
{
	HitInfo payload;
	payload.hit = 0;

	uint2 launchIndex = DispatchRaysIndex().xy;

	float depth = tDepth[launchIndex].r;

	float2 texCoord = (float2)launchIndex / DispatchRaysDimensions().xy;
	float4 clip = float4(float2(texCoord.x, 1.0f - texCoord.y) * 2.0f - 1.0f, depth, 1);
	float4 view = mul(clip, cProjectionInverse);
	view /= view.w;
	float4 world = mul(view, cViewInverse);

	float3 normal = normalize(tNormals[launchIndex].rgb);

	float3 randomVec = normalize(float3(tNoise[texCoord * 2048 % 256].xy * 2 - 1, 0));
	float3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
	float3 bitangent = cross(tangent, normal);
	float3x3 TBN = float3x3(tangent, bitangent, normal);

	int totalHits = 0;
	for(int i = 0; i < RPP_ACTUAL; ++i)
	{
		float3 n = mul(cRandomVectors[i].xyz, TBN);
		RayDesc ray;
		ray.Origin = world.xyz;
		ray.Direction = normalize(n);
		ray.TMin = 0.001f;
		ray.TMax = length(n);

		// Trace the ray
		TraceRay(
			// Parameter name: AccelerationStructure
			// Acceleration structure
			SceneBVH,

			// Parameter name: RayFlags
			// Flags can be used to specify the behavior upon hitting a surface
			RAY_FLAG_CULL_BACK_FACING_TRIANGLES
			 | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH
        	 | RAY_FLAG_FORCE_OPAQUE,

			// Parameter name: InstanceInclusionMask
			// Instance inclusion mask, which can be used to mask out some geometry to
			// this ray by and-ing the mask with a geometry mask. The 0xFF flag then
			// indicates no geometry will be masked
			0xFF,

			// Parameter name: RayContributionToHitGroupIndex
			// Depending on the type of ray, a given object can have several hit
			// groups attached (ie. what to do when hitting to compute regular
			// shading, and what to do when hitting to compute shadows). Those hit
			// groups are specified sequentially in the SBT, so the value below
			// indicates which offset (on 4 bits) to apply to the hit groups for this
			// ray. In this sample we only have one hit group per object, hence an
			// offset of 0.
			0,

			// Parameter name: MultiplierForGeometryContributionToHitGroupIndex
			// The offsets in the SBT can be computed from the object ID, its instance
			// ID, but also simply by the order the objects have been pushed in the
			// acceleration structure. This allows the application to group shaders in
			// the SBT in the same order as they are added in the AS, in which case
			// the value below represents the stride (4 bits representing the number
			// of hit groups) between two consecutive objects.
			0,

			// Parameter name: MissShaderIndex
			// Index of the miss shader to use in case several consecutive miss
			// shaders are present in the SBT. This allows to change the behavior of
			// the program when no geometry have been hit, for example one to return a
			// sky color for regular rendering, and another returning a full
			// visibility value for shadow rays. This sample has only one miss shader,
			// hence an index 0
			0,

			// Parameter name: Ray
			// Ray information to trace
			ray,

			// Parameter name: Payload
			// Payload associated to the ray, which will be used to communicate
			// between the hit/miss shaders and the raygen
			payload);
		totalHits += payload.hit;
	}

	gOutput[launchIndex] = float4((1-(float)totalHits / RPP_ACTUAL).xxx, 1.f);
}
