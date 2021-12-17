#include "CommonBindings.hlsli"

#define RootSig ROOT_SIG("CBV(b0), " \
				"DescriptorTable(UAV(u0, numDescriptors = 1)), " \
				"DescriptorTable(SRV(t0, numDescriptors = 2))")

#define THREAD_GROUP_SIZE (256)
#define KERNEL_LENGTH (4)
#define BLUR_WEIGHTS (2 * KERNEL_LENGTH + 1)
#define GS_CACHE_SIZE (KERNEL_LENGTH + THREAD_GROUP_SIZE + KERNEL_LENGTH)

struct PassParameters
{
	float2 InvDimensions;
	int Horizontal;
	float Far;
	float Near;
};

ConstantBuffer<PassParameters> cPassData : register(b0);
Texture2D tSceneDepth : register(t0);
Texture2D<float> tAmbientOcclusion : register(t1);
RWTexture2D<float> uAmbientOcclusion : register(u0);

static const float s_BlurWeights[KERNEL_LENGTH + 1] = { 1.0f, 0.9f, 0.6f, 0.15f, 0.1f };
static const float s_NormalizationFactor = 1.0f / (s_BlurWeights[0] + 2.0f * (s_BlurWeights[1] + s_BlurWeights[2] + s_BlurWeights[3] + s_BlurWeights[4]));
static const float s_BlurWeightsNormalized[BLUR_WEIGHTS] = {
	s_BlurWeights[4] * s_NormalizationFactor,
	s_BlurWeights[3] * s_NormalizationFactor,
	s_BlurWeights[2] * s_NormalizationFactor,
	s_BlurWeights[1] * s_NormalizationFactor,
	s_BlurWeights[0] * s_NormalizationFactor,
	s_BlurWeights[1] * s_NormalizationFactor,
	s_BlurWeights[2] * s_NormalizationFactor,
	s_BlurWeights[3] * s_NormalizationFactor,
	s_BlurWeights[4] * s_NormalizationFactor,
};
groupshared float gAoCache[GS_CACHE_SIZE];
groupshared float gDepthCache[GS_CACHE_SIZE];

[RootSignature(RootSig)]
[numthreads(THREAD_GROUP_SIZE, 1, 1)]
void CSMain(uint3 groupId : SV_GroupID, uint groupIndex : SV_GroupIndex)
{
	float2 direction = float2(cPassData.Horizontal, 1 - cPassData.Horizontal);
	uint2 multiplier = direction * THREAD_GROUP_SIZE + 1 * (1 - direction);
	uint2 groupBegin = groupId.xy * multiplier;

	int i;
	for (i = groupIndex; i < GS_CACHE_SIZE; i += THREAD_GROUP_SIZE)
	{
		float2 uv = ((float2)groupBegin + 0.5f + direction * (i - KERNEL_LENGTH)) * cPassData.InvDimensions;
		gAoCache[i] = tAmbientOcclusion.SampleLevel(sLinearClamp, uv, 0).r;
		gDepthCache[i] = LinearizeDepth01(tSceneDepth.SampleLevel(sLinearClamp, uv, 0).r, cPassData.Near, cPassData.Far);
	}

	GroupMemoryBarrierWithGroupSync();

	uint center = groupIndex + KERNEL_LENGTH;
	float currentAo = gAoCache[center];
	float currentSceneDepth = gDepthCache[center];

	float avgOcclusion = 0;
	for(i = 0; i < BLUR_WEIGHTS; ++i)
	{
		uint samplePoint = center + i - KERNEL_LENGTH;
		float depth = gDepthCache[samplePoint];
		float weight = saturate(abs(depth - currentSceneDepth));
		avgOcclusion += lerp(gAoCache[samplePoint], currentAo, weight) * s_BlurWeightsNormalized[i];
	}

	int2 pixel = groupBegin + groupIndex * direction;
	uAmbientOcclusion[pixel] = avgOcclusion
	;
}
