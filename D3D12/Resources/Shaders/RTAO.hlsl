#include "RNG.hlsli"
#include "Common.hlsli"

#define RPP 64

RWTexture2D<float> gOutput : register(u0);

RaytracingAccelerationStructure SceneBVH : register(t0);

Texture2D tNormals : register(t1);
Texture2D tDepth : register(t2);

SamplerState sSceneSampler : register(s0);

cbuffer ShaderParameters : register(b0)
{
	float4x4 cViewInverse;
	float4x4 cProjectionInverse;
	float4 cRandomVectors[RPP];
	float cPower;
	float cRadius;
	int cSamples;
}

struct RayPayload
{
	float hitDistance;
};

[shader("closesthit")] 
void ClosestHit(inout RayPayload payload, BuiltInTriangleIntersectionAttributes attrib) 
{
	payload.hitDistance = RayTCurrent();
}

[shader("miss")] 
void Miss(inout RayPayload payload : SV_RayPayload) 
{
	payload.hitDistance = 0;
}

[shader("raygeneration")] 
void RayGen() 
{
	RayPayload payload = (RayPayload)0;

	uint2 launchIndex = DispatchRaysIndex().xy;
	uint launchIndex1d = launchIndex.x + launchIndex.y * DispatchRaysDimensions().x;
	float2 texCoord = (float2)launchIndex / DispatchRaysDimensions().xy;

	float depth = tDepth.SampleLevel(sSceneSampler, texCoord, 0).r;
	float3 normal = tNormals.SampleLevel(sSceneSampler, texCoord, 0).rgb;
 	
	uint state = SeedThread(launchIndex1d);
	float3 randomVec = float3(Random01(state), Random01(state), Random01(state)) * 2.0f - 1.0f;

	float3 world = WorldFromDepth(texCoord, depth, mul(cProjectionInverse, cViewInverse));
	float3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
	float3 bitangent = cross(tangent, normal);
	float3x3 TBN = float3x3(tangent, bitangent, normal);
	
	float accumulatedAo = 0.0f;
	for(int i = 0; i < cSamples; ++i)
	{
		float3 n = mul(cRandomVectors[Random(state, 0, RPP - 1)].xyz, TBN);
		RayDesc ray;
		ray.Origin = world + 0.001f * n;
		ray.Direction = n;
		ray.TMin = 0.0f;
		ray.TMax = cRadius;

		// Trace the ray
		TraceRay(
			//AccelerationStructure
			SceneBVH,

			//RayFlags
			RAY_FLAG_CULL_BACK_FACING_TRIANGLES |
        	RAY_FLAG_FORCE_OPAQUE,

			//InstanceInclusionMask
			// Instance inclusion mask, which can be used to mask out some geometry to this ray by
  			// and-ing the mask with a geometry mask. The 0xFF flag then indicates no geometry will be
			// masked
			0xFF,

			//RayContributionToHitGroupIndex
			// Depending on the type of ray, a given object can have several hit groups attached
			// (ie. what to do when hitting to compute regular shading, and what to do when hitting
			// to compute shadows). Those hit groups are specified sequentially in the SBT, so the value
			// below indicates which offset (on 4 bits) to apply to the hit groups for this ray.
			0,

			//MultiplierForGeometryContributionToHitGroupIndex
			// The offsets in the SBT can be computed from the object ID, its instance ID, but also simply
			// by the order the objects have been pushed in the acceleration structure. This allows the
			// application to group shaders in the SBT in the same order as they are added in the AS, in
			// which case the value below represents the stride (4 bits representing the number of hit
			// groups) between two consecutive objects.
			0,

			//MissShaderIndex
			// Index of the miss shader to use in case several consecutive miss shaders are present in the
			// SBT. This allows to change the behavior of the program when no geometry have been hit, for
			// example one to return a sky color for regular rendering, and another returning a full
			// visibility value for shadow rays.
			0,

			//Ray
			ray,

			//Payload
			payload);

		accumulatedAo += payload.hitDistance != 0;
	}
	accumulatedAo /= cSamples;
	gOutput[launchIndex] = pow(saturate(1 - accumulatedAo), cPower);
}
