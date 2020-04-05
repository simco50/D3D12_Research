#include "RNG.hlsli"

#define RPP 64

RWTexture2D<float> gOutput : register(u0);

RaytracingAccelerationStructure SceneBVH : register(t0);

Texture2D tNormals : register(t1);
Texture2D tDepth : register(t2);
Texture2D tNoise : register(t3);

SamplerState sSceneSampler : register(s0);
SamplerState sTexSampler : register(s1);

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

float4 ClipToWorld(float4 clip)
{
	float4 view = mul(clip, cProjectionInverse);
	view /= view.w;
	return mul(view, cViewInverse);
}

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

	float4 world = ClipToWorld(float4(float2(texCoord.x, 1.0f - texCoord.y) * 2.0f - 1.0f, depth, 1));
	float3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
	float3 bitangent = cross(tangent, normal);
	float3x3 TBN = float3x3(tangent, bitangent, normal);
	
	float accumulatedAo = 0.0f;
	for(int i = 0; i < cSamples; ++i)
	{
		float3 n = mul(cRandomVectors[Random(state, 0, RPP - 1)].xyz, TBN);
		RayDesc ray;
		ray.Origin = world.xyz + 0.001f * n;
		ray.Direction = n;
		ray.TMin = 0.0f;
		ray.TMax = cRadius;

		// Trace the ray
		TraceRay(
			//AccelerationStructure
			SceneBVH,
			//RayFlags
			RAY_FLAG_CULL_BACK_FACING_TRIANGLES |
			RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
        	RAY_FLAG_FORCE_OPAQUE,
			//InstanceInclusionMask
			0xFF,
			//RayContributionToHitGroupIndex
			0,
			//MultiplierForGeometryContributionToHitGroupIndex
			0,
			//MissShaderIndex
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
