#include "Common.hlsli"
#include "RaytracingCommon.hlsli"
#include "Random.hlsli"

struct PassData
{
	float Power;
	float Radius;
	uint Samples;
	RWTexture2DH<float> Output;
	Texture2DH<float> SceneDepth;
};
DEFINE_CONSTANTS(PassData, 0);

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
	float2 uv = TexelToUV(launchIndex, dimInv);

	float3 world = WorldPositionFromDepth(uv, cPassData.SceneDepth.SampleLevel(sPointClamp, uv, 0).r, cView.ClipToWorld);
	float3 viewNormal = ViewNormalFromDepth(uv, cPassData.SceneDepth.Get(), NormalReconstructMethod::Taps5);
	float3 normal = mul(viewNormal, (float3x3)cView.ViewToWorld);

	uint seed = SeedThread(launchIndex, launchDim, cView.FrameIndex);

	const float3x3 tangentM = TangentMatrix(normal);

	// Diffuse reflections integral is over (1 / PI) * Li * NdotL
	// We sample a cosine weighted distribution over the hemisphere which has a PDF which conveniently cancels out the inverse PI and NdotL terms.

	uint numSamples = cPassData.Samples;
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
		ray.TMax = cPassData.Radius;
		float hit = TraceOcclusionRay(ray, cView.TLAS.Get());

		accumulatedAo += hit;
	}
	accumulatedAo /= numSamples;
	cPassData.Output.Store(launchIndex, 1 - (saturate(1 - accumulatedAo) * cPassData.Power));
}
