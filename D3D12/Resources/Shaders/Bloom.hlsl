#include "CommonBindings.hlsli"

struct SeparateData
{
	float Threshold;
	float BrightnessClamp;
};

struct MipChainData
{
	uint SourceMip;
	float2 TargetDimensionsInv;
	int Horizontal;
};

ConstantBuffer<SeparateData> cSeparateData : register(b0);
ConstantBuffer<MipChainData> cMipData : register(b0);

Texture2D<float4> tSource : register(t0);
StructuredBuffer<float> tAverageLuminance : register(t1);
RWTexture2D<float4> uTarget : register(u0);

[numthreads(8, 8, 1)]
void SeparateBloomCS(uint3 threadId : SV_DispatchThreadID)
{
	float2 UV = (0.5f + threadId.xy) * cView.ViewportDimensionsInv;
	float4 color = 0;
	color += tSource.SampleLevel(sLinearClamp, UV, 0, int2(-1, -1));
	color += tSource.SampleLevel(sLinearClamp, UV, 0, int2(1, -1));
	color += tSource.SampleLevel(sLinearClamp, UV, 0, int2(-1, 1));
	color += tSource.SampleLevel(sLinearClamp, UV, 0, int2(1, 1));
	color *= 0.25f;

	float exposure = tAverageLuminance[2];
	color *= exposure;

	color = min(color, cSeparateData.BrightnessClamp);
	color = max(color - cSeparateData.Threshold, 0);

	uTarget[threadId.xy] = float4(color.rgb, 1);
}

#define THREAD_GROUP_SIZE (128)
#define KERNEL_LENGTH (16)
#define BLUR_WEIGHTS (2 * KERNEL_LENGTH + 1)
#define GS_CACHE_SIZE (KERNEL_LENGTH + THREAD_GROUP_SIZE + KERNEL_LENGTH)

static const float s_BlurWeights[BLUR_WEIGHTS] = {
	0.004013,
	0.005554,
	0.007527,
	0.00999,
	0.012984,
	0.016524,
	0.020594,
	0.025133,
	0.030036,
	0.035151,
	0.040283,
	0.045207,
	0.049681,
	0.053463,
	0.056341,
	0.058141,
	0.058754,
	0.058141,
	0.056341,
	0.053463,
	0.049681,
	0.045207,
	0.040283,
	0.035151,
	0.030036,
	0.025133,
	0.020594,
	0.016524,
	0.012984,
	0.00999,
	0.007527,
	0.005554,
	0.004013
};

groupshared float4 gsSampleCache[GS_CACHE_SIZE];

[numthreads(THREAD_GROUP_SIZE, 1, 1)]
void BloomMipChainCS(uint3 groupId : SV_GroupID, uint groupIndex : SV_GroupIndex)
{
	float2 direction = float2(cMipData.Horizontal, 1 - cMipData.Horizontal);
	uint2 multiplier = direction * THREAD_GROUP_SIZE + 1 * (1 - direction);
	uint2 groupBegin = groupId.xy * multiplier;

	int i;
	for (i = groupIndex; i < GS_CACHE_SIZE; i += THREAD_GROUP_SIZE)
	{
		float2 uv = ((float2)groupBegin + 0.5f + direction * (i - KERNEL_LENGTH)) * cMipData.TargetDimensionsInv;
		gsSampleCache[i] = tSource.SampleLevel(sLinearClamp, uv, cMipData.SourceMip);
	}

	GroupMemoryBarrierWithGroupSync();

	uint center = groupIndex + KERNEL_LENGTH;
	float4 currentSample = gsSampleCache[center];

	float4 value = 0;
	for(i = 0; i < BLUR_WEIGHTS; ++i)
	{
		uint samplePoint = center + i - KERNEL_LENGTH;
		value += gsSampleCache[samplePoint] * s_BlurWeights[i];
	}

	int2 pixel = groupBegin + groupIndex * direction;
	uTarget[pixel] = value;
}
