#include "Common.hlsli"

#define EPSILON 0.0001f
#define HISTOGRAM_THREADS_PER_DIMENSION 16

struct PassParams
{
	uint Width;
	uint Height;
	float MinLogLuminance;
	float OneOverLogLuminanceRange;
	Texture2DH<float4> HDRTexture;
	RWTypedBufferH<uint> LuminanceHistogram;
};
DEFINE_CONSTANTS(PassParams, 0);

uint HDRToHistogramBin(float3 hdrColor)
{
	float luminance = GetLuminance(hdrColor);

	if(luminance < EPSILON)
	{
		return 0;
	}

	float logLuminance = saturate((log2(luminance) - cPassParams.MinLogLuminance) * cPassParams.OneOverLogLuminanceRange);
	return (uint)(logLuminance * (NUM_HISTOGRAM_BINS - 1) + 1.0);
}

groupshared uint HistogramShared[NUM_HISTOGRAM_BINS];

[numthreads(HISTOGRAM_THREADS_PER_DIMENSION, HISTOGRAM_THREADS_PER_DIMENSION, 1)]
void CSMain(uint groupIndex : SV_GroupIndex, uint3 threadId : SV_DispatchThreadID)
{
	HistogramShared[groupIndex] = 0;

	GroupMemoryBarrierWithGroupSync();

	if(threadId.x < cPassParams.Width && threadId.y < cPassParams.Height)
	{
		float3 hdrColor = cPassParams.HDRTexture.Load(int3(threadId.xy, 0)).rgb;
		uint binIndex = HDRToHistogramBin(hdrColor);
		InterlockedAdd(HistogramShared[binIndex], 1);
	}

	GroupMemoryBarrierWithGroupSync();

	InterlockedAdd(cPassParams.LuminanceHistogram.Get()[groupIndex], HistogramShared[groupIndex]);
}
