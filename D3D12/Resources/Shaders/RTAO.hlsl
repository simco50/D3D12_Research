#include "RNG.hlsli"
#include "Common.hlsli"

#define RPP 64
RWTexture2D<float> uOutput : register(u0);

RaytracingAccelerationStructure SceneBVH : register(t0);

Texture2D tDepth : register(t1);

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

	float2 dimInv = rcp((float2)DispatchRaysDimensions().xy);
	uint2 launchIndex = DispatchRaysIndex().xy;
	uint launchIndex1d = launchIndex.x + launchIndex.y * DispatchRaysDimensions().x;
	float2 texCoord = (float2)launchIndex * dimInv;

	float3 world = WorldFromDepth(texCoord, tDepth.SampleLevel(sSceneSampler, texCoord, 0).r, mul(cProjectionInverse, cViewInverse));
    float3 normal = NormalFromDepth(tDepth, sSceneSampler, texCoord, dimInv, cProjectionInverse);
 	
	uint state = SeedThread(launchIndex1d);
	float3 randomVec = float3(Random01(state), Random01(state), Random01(state)) * 2.0f - 1.0f;

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

		TraceRay(
			SceneBVH, 														//AccelerationStructure
			RAY_FLAG_CULL_BACK_FACING_TRIANGLES | RAY_FLAG_FORCE_OPAQUE, 	//RayFlags
			0xFF, 															//InstanceInclusionMask
			0, 																//RayContributionToHitGroupIndex
			0, 																//MultiplierForGeometryContributionToHitGroupIndex
			0, 																//MissShaderIndex
			ray, 															//Ray
			payload 														//Payload
		);

		accumulatedAo += payload.hitDistance != 0;
	}
	accumulatedAo /= cSamples;
	uOutput[launchIndex] = pow(saturate(1 - accumulatedAo), cPower);
}
