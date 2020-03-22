#define RPP 64
#define RPP_ACTUAL 4 

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
	float2 texCoord = (float2)launchIndex / DispatchRaysDimensions().xy;

	float depth = tDepth.SampleLevel(sSceneSampler, texCoord, 0).r;
	float3 normal = tNormals.SampleLevel(sSceneSampler, texCoord, 0).rgb;
	float3 noise = float3(tNoise.SampleLevel(sTexSampler, texCoord * 10, 0).rg * 2 - 1, 0);

	float4 world = ClipToWorld(float4(float2(texCoord.x, 1.0f - texCoord.y) * 2.0f - 1.0f, depth, 1));
	float3 tangent = normalize(noise - normal * dot(noise, normal));
	float3 bitangent = cross(tangent, normal);
	float3x3 TBN = float3x3(tangent, bitangent, normal);

	float accumulatedAo = 0.0f;
	for(int i = 0; i < RPP_ACTUAL; ++i)
	{
		float3 n = mul(cRandomVectors[i].xyz, TBN);
		RayDesc ray;
		ray.Origin = world.xyz + 0.001f * n;
		ray.Direction = n;
		ray.TMin = 0.0f;
		ray.TMax = 1.0f;

		// Trace the ray
		TraceRay(
			//AccelerationStructure
			SceneBVH,
			//RayFlags
			RAY_FLAG_CULL_BACK_FACING_TRIANGLES
			 | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH
        	 | RAY_FLAG_FORCE_OPAQUE,
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
	accumulatedAo /= RPP_ACTUAL;
	gOutput[launchIndex] = 1 - accumulatedAo;
}
