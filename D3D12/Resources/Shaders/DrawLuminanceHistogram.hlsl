#include "CommonBindings.hlsli"
#include "TonemappingCommon.hlsli"

#define BACKGROUND_COLOR float4(0, 0, 0, 0.6f)
#define FOREGROUND_COLOR float4(1, 0, 0, 0.8f)
#define CURSOR_COLOR float4(1, 0, 1, 1.0f)
#define TARGET_COLOR float4(1, 1, 1, 1.0f)

struct PassParameters
{
	float MinLogLuminance;
	float InverseLogLuminanceRange;
};

ConstantBuffer<PassParameters> cPassData : register(b0);
RWTexture2D<float4> uOutTexture : register(u0);
ByteAddressBuffer tLuminanceHistogram : register(t0);
StructuredBuffer<float> tAverageLuminance : register(t1);

groupshared uint gsHistogram[NUM_HISTOGRAM_BINS];

void BlendPixel(uint2 location, uint2 offset, float4 color)
{
	uOutTexture[location + offset] = uOutTexture[location + offset] * (1 - color.a) + color * color.a;
}

[numthreads(NUM_HISTOGRAM_BINS, 1, 1)]
void DrawLuminanceHistogram(uint groupIndex : SV_GroupIndex, uint3 threadId : SV_DispatchThreadID)
{
	const uint binDrawSize = 4;

	uint maxBinValue = 1;
	uint currentBinValue = tLuminanceHistogram.Load(groupIndex * binDrawSize);
	gsHistogram[groupIndex] = currentBinValue;
	GroupMemoryBarrierWithGroupSync();
	for(int i = 0; i < NUM_HISTOGRAM_BINS; ++i)
	{
		maxBinValue = max(maxBinValue, gsHistogram[i]);
	}
	uint2 dimensions = uint2(NUM_HISTOGRAM_BINS * binDrawSize, NUM_HISTOGRAM_BINS);
	uint2 s = uint2(binDrawSize * groupIndex, threadId.y);

	float currentAverage = tAverageLuminance[0];
	float targetAverage = tAverageLuminance[1];
	float tCurrent = (log2(currentAverage) - cPassData.MinLogLuminance) * cPassData.InverseLogLuminanceRange;
	float tTarget = (log2(targetAverage) - cPassData.MinLogLuminance) * cPassData.InverseLogLuminanceRange;

	float4 outColor = BACKGROUND_COLOR;
	if((uint)floor(tCurrent * NUM_HISTOGRAM_BINS) == groupIndex)
	{
		outColor = CURSOR_COLOR;
	}
	else if((uint)floor(tTarget * NUM_HISTOGRAM_BINS) == groupIndex)
	{
		outColor = TARGET_COLOR;
	}
	else if(NUM_HISTOGRAM_BINS - threadId.y < (currentBinValue * NUM_HISTOGRAM_BINS) / maxBinValue)
	{
		outColor = FOREGROUND_COLOR;
	}
	BlendPixel(s, uint2(0, 0), BACKGROUND_COLOR);
	BlendPixel(s, uint2(1, 0), outColor);
	BlendPixel(s, uint2(2, 0), outColor);
	BlendPixel(s, uint2(3, 0), BACKGROUND_COLOR);
}
