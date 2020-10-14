#include "Common.hlsli"

#define RootSig "CBV(b0, visibility=SHADER_VISIBILITY_ALL), " \
				"DescriptorTable(UAV(u0, numDescriptors = 1), visibility=SHADER_VISIBILITY_ALL), " \
				"DescriptorTable(SRV(t0, numDescriptors = 2), visibility=SHADER_VISIBILITY_ALL), " \
				"StaticSampler(s0, filter=FILTER_MIN_MAG_LINEAR_MIP_POINT, visibility = SHADER_VISIBILITY_ALL, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP), " \

#define THREAD_GROUP_SIZE (256)
#define KERNEL_LENGTH (4)
#define BLUR_WEIGHTS (2 * KERNEL_LENGTH + 1)
#define GS_CACHE_SIZE (KERNEL_LENGTH + THREAD_GROUP_SIZE + KERNEL_LENGTH)

cbuffer ShaderParameters : register(b0)
{
    float2 cInvDimensions;
    int cHorizontal;
    float cFar;
    float cNear;
}

SamplerState sSampler : register(s0);

Texture2D tDepth : register(t0);
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

struct CS_INPUT
{
    uint3 GroupId : SV_GROUPID;
    uint3 GroupThreadId : SV_GROUPTHREADID;
    uint3 DispatchThreadId : SV_DISPATCHTHREADID;
    uint GroupIndex : SV_GROUPINDEX;
};

[RootSignature(RootSig)]
[numthreads(THREAD_GROUP_SIZE, 1, 1)]
void CSMain(CS_INPUT input)
{
    float2 direction = float2(cHorizontal, 1 - cHorizontal);
    uint2 multiplier = direction * THREAD_GROUP_SIZE + 1 * (1 - direction);
    uint2 groupBegin = input.GroupId.xy * multiplier;

	int i;
	for (i = input.GroupIndex; i < GS_CACHE_SIZE; i += THREAD_GROUP_SIZE)
	{
		float2 uv = ((float2)groupBegin + 0.5f + direction * (i - KERNEL_LENGTH)) * cInvDimensions;
		gAoCache[i] = tAmbientOcclusion.SampleLevel(sSampler, uv, 0).r;
		gDepthCache[i] = LinearizeDepth01(tDepth.SampleLevel(sSampler, uv, 0).r, cNear, cFar);
	}

	GroupMemoryBarrierWithGroupSync();

    uint center = input.GroupIndex + KERNEL_LENGTH;
    float currentAo = gAoCache[center];
    float currentDepth = gDepthCache[center];

    float avgOcclusion = 0;
    for(i = 0; i < BLUR_WEIGHTS; ++i)
    {
        uint samplePoint = center + i - KERNEL_LENGTH;
        float depth = gDepthCache[samplePoint];
        float weight = saturate(abs(depth - currentDepth));
        avgOcclusion += lerp(gAoCache[samplePoint], currentAo, weight) * s_BlurWeightsNormalized[i];
    }

    int2 pixel = groupBegin + input.GroupIndex * direction;
    uAmbientOcclusion[pixel] = avgOcclusion;
}