#include "Common.hlsli"
#include "RaytracingCommon.hlsli"
#include "Random.hlsli"

RWTexture2D<float> uOutput : register(u0);
Texture2D<float> tSceneDepth : register(t0);

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

[shader("raygeneration")]
void RayGen()
{
	uint2 launchDim = DispatchRaysDimensions().xy;
	float2 dimInv = rcp((float2)launchDim.xy);
	uint2 launchIndex = DispatchRaysIndex().xy;
	uint launchIndex1d = launchIndex.x + launchIndex.y * launchDim.x;
	float2 uv = (launchIndex + 0.5f) * dimInv;

	float3 world = WorldPositionFromDepth(uv, tSceneDepth.SampleLevel(sPointClamp, uv, 0).r, cView.ViewProjectionInverse);
	float3 viewNormal = ViewNormalFromDepth(uv, tSceneDepth, NormalReconstructMethod::Taps5);
	float3 normal = mul(viewNormal, (float3x3)cView.ViewInverse);

	uint seed = SeedThread(launchIndex, launchDim, cView.FrameIndex);

	const float3x3 tangentM = TangentMatrix(normal);

	// Diffuse reflections integral is over (1 / PI) * Li * NdotL
	// We sample a cosine weighted distribution over the hemisphere which has a PDF which conveniently cancels out the inverse PI and NdotL terms.

	uint numSamples = cPass.Samples;
	float accumulatedAo = 0.0f;
	for(int i = 0; i < numSamples; ++i)
	{
		float2 u = float2(Random01(seed), Random01(seed));
		float pdf;
		float3 randomDirection = mul(HemisphereSampleCosineWeight(u, pdf), tangentM);

		RayDesc ray;
		ray.Origin = world;
		ray.Direction = randomDirection;
		ray.TMin = RAY_BIAS;
		ray.TMax = cPass.Radius;
		RaytracingAccelerationStructure tlas = ResourceDescriptorHeap[cView.TLASIndex];
		float hit = TraceOcclusionRay(ray, tlas);

		accumulatedAo += hit;
	}
	accumulatedAo /= numSamples;
	uOutput[launchIndex] = 1 - (saturate(1 - accumulatedAo) * cPass.Power);
}
