#include "Common.hlsli"
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

float3x3 TangentMatrix(float3 z)
{
    float3 ref = abs(dot(z, float3(0, 1, 0))) > 0.99f ? float3(0, 0, 1) : float3(0, 1, 0);
    float3 x = normalize(cross(ref, z));
    float3 y = cross(z, x);
    return float3x3(x, y, z);
}

float3 RandomCosineWeightedRay(float3 n, inout uint seed)
{
	float2 r = float2(Random01(seed), Random01(seed));
    float2 rand_sample = max(0.00001f, r);
    float phi = 2.0f * PI * rand_sample.y;
    float cos_theta = sqrt(rand_sample.x);
    float sin_theta = sqrt(1 - rand_sample.x);
    float3 t = float3(sin_theta * cos(phi), sin_theta * sin(phi), cos_theta);
    return normalize(mul(t, TangentMatrix(n)));
}

[shader("raygeneration")]
void RayGen()
{
	uint2 launchDim = DispatchRaysDimensions().xy;
	float2 dimInv = rcp((float2)launchDim.xy);
	uint2 launchIndex = DispatchRaysIndex().xy;
	uint launchIndex1d = launchIndex.x + launchIndex.y * launchDim.x;
	float2 uv = (launchIndex + 0.5f) * dimInv;

	float3 world = WorldFromDepth(uv, tSceneDepth.SampleLevel(sLinearClamp, uv, 0).r, cView.ViewProjectionInverse);
	float3 normal = NormalFromDepth(tSceneDepth, sLinearClamp, uv, dimInv, cView.ProjectionInverse);
	normal = mul(normal, (float3x3)cView.ViewInverse);

	uint randSeed = SeedThread(launchIndex, launchDim, cView.FrameIndex);

	float accumulatedAo = 0.0f;
	for(int i = 0; i < cPass.Samples; ++i)
	{
		float3 randomDirection = RandomCosineWeightedRay(normal.xyz, randSeed);
		
		RayDesc ray;
		ray.Origin = world;
		ray.Direction = randomDirection;
		ray.TMin = RAY_BIAS;
		ray.TMax = cPass.Radius;
		RaytracingAccelerationStructure tlas = ResourceDescriptorHeap[cView.TLASIndex];
		float hit = !TraceOcclusionRay(ray, tlas);

		accumulatedAo += hit;
	}
	accumulatedAo /= cPass.Samples;
	uOutput[launchIndex] = pow(saturate(1 - accumulatedAo), cPass.Power);
}
