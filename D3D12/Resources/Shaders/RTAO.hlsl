#include "RNG.hlsli"
#include "Common.hlsli"
#include "CommonBindings.hlsli"

GlobalRootSignature GlobalRootSig =
{
	"CBV(b0, visibility=SHADER_VISIBILITY_ALL),"
	"DescriptorTable(UAV(u0, numDescriptors = 1), visibility=SHADER_VISIBILITY_ALL),"
	"DescriptorTable(SRV(t0, numDescriptors = 1), visibility=SHADER_VISIBILITY_ALL),"
	GLOBAL_BINDLESS_TABLE ", "
	"StaticSampler(s0, filter=FILTER_MIN_MAG_LINEAR_MIP_POINT, visibility = SHADER_VISIBILITY_ALL),"
};

#define RPP 64
RWTexture2D<float> uOutput : register(u0);
Texture2D tSceneDepth : register(t0);
SamplerState sSceneSampler : register(s0);

cbuffer ShaderParameters : register(b0)
{
	float4x4 cViewInverse;
	float4x4 cProjectionInverse;
	float4 cRandomVectors[RPP];
	float cPower;
	float cRadius;
	int cSamples;
	uint TLASIndex;
}

struct RayPayload
{
	int hit;
};

[shader("miss")] 
void Miss(inout RayPayload payload : SV_RayPayload) 
{
	payload.hit = 0;
}

[shader("raygeneration")] 
void RayGen() 
{
	RayPayload payload = (RayPayload)0;
	payload.hit = 1;

	float2 dimInv = rcp((float2)DispatchRaysDimensions().xy);
	uint2 launchIndex = DispatchRaysIndex().xy;
	uint launchIndex1d = launchIndex.x + launchIndex.y * DispatchRaysDimensions().x;
	float2 texCoord = (float2)launchIndex * dimInv;

	float3 world = WorldFromDepth(texCoord, tSceneDepth.SampleLevel(sSceneSampler, texCoord, 0).r, mul(cProjectionInverse, cViewInverse));
    float3 normal = NormalFromDepth(tSceneDepth, sSceneSampler, texCoord, dimInv, cProjectionInverse);
 	
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
		ray.Origin = world + 0.01f * n;
		ray.Direction = n;
		ray.TMin = 0.0f;
		ray.TMax = cRadius;

		TraceRay(
			tTLASTable[TLASIndex], 											//AccelerationStructure
			RAY_FLAG_CULL_BACK_FACING_TRIANGLES | RAY_FLAG_FORCE_OPAQUE, 	//RayFlags
			0xFF, 															//InstanceInclusionMask
			0, 																//RayContributionToHitGroupIndex
			0, 																//MultiplierForGeometryContributionToHitGroupIndex
			0, 																//MissShaderIndex
			ray, 															//Ray
			payload 														//Payload
		);

		accumulatedAo += payload.hit;
	}
	accumulatedAo /= cSamples;
	uOutput[launchIndex] = pow(saturate(1 - accumulatedAo), cPower);
}
