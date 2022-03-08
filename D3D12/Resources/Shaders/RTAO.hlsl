#include "CommonBindings.hlsli"
#include "RaytracingCommon.hlsli"
#include "Random.hlsli"

RWTexture2D<float> uOutput : register(u0);
Texture2D tSceneDepth : register(t0);

struct PassData
{
	float Power;
	float Radius;
	uint Samples;
};

ConstantBuffer<PassData> cPass : register(b0);

struct RAYPAYLOAD RayPayload
{
	float hit RAYQUALIFIER(read(caller) : write(caller, miss));
};

// Utility function to get a vector perpendicular to an input vector
// From Michael M. Stark - https://blog.selfshadow.com/2011/10/17/perp-vectors/
float3 GetPerpendicularVector(float3 u)
{
	float3 a = abs(u);
	uint uyx = sign(a.x - a.y);
	uint uzx = sign(a.x - a.z);
	uint uzy = sign(a.y - a.z);

	uint xm = uyx & uzx;
	uint ym = (1^xm) & uzy;
	uint zm = 1^(xm & ym);

	float3 v = cross(u, float3(xm, ym, zm));
	return v;
}

// Get a cosine-weighted random vector centered around a specified normal direction.
float3 GetCosHemisphereSample(inout uint randSeed, float3 hitNorm)
{
	// Get 2 random numbers to select our sample with
	float2 randVal = float2(Random01(randSeed), Random01(randSeed));

	// Cosine weighted hemisphere sample from RNG
	float3 bitangent = GetPerpendicularVector(hitNorm);
	float3 tangent = cross(bitangent, hitNorm);
	float r = sqrt(randVal.x);
	float phi = 2.0f * 3.14159265f * randVal.y;

	// Get our cosine-weighted hemisphere lobe sample direction
	return tangent * (r * cos(phi)) + bitangent * (r * sin(phi)) + hitNorm.xyz * sqrt(1 - randVal.x);
}

[shader("miss")]
void Miss(inout RayPayload payload : SV_RayPayload)
{
	payload.hit = 0.0f;
}

[shader("raygeneration")]
void RayGen()
{
	uint2 launchDim = DispatchRaysDimensions().xy;
	float2 dimInv = rcp((float2)launchDim.xy);
	uint2 launchIndex = DispatchRaysIndex().xy;
	uint launchIndex1d = launchIndex.x + launchIndex.y * launchDim.x;
	float2 uv = (float2)launchIndex * dimInv;

	float3 world = WorldFromDepth(uv, tSceneDepth.SampleLevel(sLinearClamp, uv, 0).r, cView.ViewProjectionInverse);
	float3 normal = NormalFromDepth(tSceneDepth, sLinearClamp, uv, dimInv, cView.ViewProjectionInverse);

	uint randSeed = SeedThread(launchIndex, launchDim, cView.FrameIndex);

	float accumulatedAo = 0.0f;
	for(int i = 0; i < cPass.Samples; ++i)
	{
		RayPayload payload;
		payload.hit = 1.0f;
		float3 randomDirection = GetCosHemisphereSample(randSeed, normal.xyz);

		RayDesc ray;
		ray.Origin = world;
		ray.Direction = randomDirection;
		ray.TMin = RAY_BIAS;
		ray.TMax = cPass.Radius;

		RaytracingAccelerationStructure tlas = ResourceDescriptorHeap[cView.TLASIndex];

		TraceRay(
			tlas, 															//AccelerationStructure
										//RayFlags
				RAY_FLAG_FORCE_OPAQUE |
				RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
				RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
			0xFF, 															//InstanceInclusionMask
			0, 																//RayContributionToHitGroupIndex
			0, 																//MultiplierForGeometryContributionToHitGroupIndex
			0, 																//MissShaderIndex
			ray, 															//Ray
			payload 														//Payload
		);
		float hit = payload.hit;
		accumulatedAo += hit;
	}
	accumulatedAo /= cPass.Samples;
	uOutput[launchIndex] = pow(saturate(1 - accumulatedAo), cPass.Power);
}
