#include "Common.hlsli"
#include "ShaderDebugRender.hlsli"

#define HISTOGRAM_AVERAGE_THREADS_PER_DIMENSION 16

struct PassParameters
{
	uint PixelCount;
	float MinLogLuminance;
	float LogLuminanceRange;
	float TimeDelta;
	float Tau;
};

Buffer<uint> tLuminanceHistogram : register(t0);
RWStructuredBuffer<float> uLuminanceOutput : register(u0);
ConstantBuffer<PassParameters> cPassParameters : register(b0);

groupshared float gHistogramShared[NUM_HISTOGRAM_BINS];

float EV100FromLuminance(float luminance)
{
	//https://google.github.io/filament/Filament.md.html#imagingpipeline/physicallybasedcamera/exposurevalue
	const float K = 12.5f; //Reflected-light meter constant
	const float ISO = 100.0f;
	return log2(luminance * (ISO / K));
}

float Exposure(float ev100)
{
	//https://google.github.io/filament/Filament.md.html#imagingpipeline/physicallybasedcamera/exposurevalue
	return 1.0f / (pow(2.0f, ev100) * 1.2f);
}

float Adaption(float current, float target, float dt, float speed)
{
	float factor = 1.0f - exp2(-dt * speed);
	return current + (target - current) * factor;
}

[numthreads(HISTOGRAM_AVERAGE_THREADS_PER_DIMENSION, HISTOGRAM_AVERAGE_THREADS_PER_DIMENSION, 1)]
void CSMain(uint groupIndex : SV_GroupIndex)
{
	float countForThisBin = (float)tLuminanceHistogram[groupIndex];
	gHistogramShared[groupIndex] = countForThisBin * groupIndex;
	GroupMemoryBarrierWithGroupSync();

	[unroll]
	for(uint histogramSampleIndex = (NUM_HISTOGRAM_BINS >> 1); histogramSampleIndex > 0; histogramSampleIndex >>= 1)
	{
		if(groupIndex < histogramSampleIndex)
		{
			gHistogramShared[groupIndex] += gHistogramShared[groupIndex + histogramSampleIndex];
		}
		GroupMemoryBarrierWithGroupSync();
	}

	if(groupIndex == 0)
	{
		float weightedLogAverage = (gHistogramShared[0].x / max((float)cPassParameters.PixelCount - countForThisBin, 1.0)) - 1.0;
		float weightedAverageLuminance = exp2(((weightedLogAverage / (NUM_HISTOGRAM_BINS - 1)) * cPassParameters.LogLuminanceRange) + cPassParameters.MinLogLuminance);
		float luminanceLastFrame = uLuminanceOutput[0];
		float adaptedLuminance = Adaption(luminanceLastFrame, weightedAverageLuminance, cPassParameters.TimeDelta, cPassParameters.Tau);
		float exposure = Exposure(EV100FromLuminance(adaptedLuminance));

		uLuminanceOutput[0] = adaptedLuminance;
		uLuminanceOutput[1] = weightedAverageLuminance;
		uLuminanceOutput[2] = exposure;

#define DEBUG 0
#if DEBUG
		TextWriter writer = CreateTextWriter(20);
		writer.Text(TEXT("Adapted Luminance: "));
		writer.Float(adaptedLuminance);
		writer.NewLine();

		writer.Text(TEXT("Average Luminance: "));
		writer.Float(weightedAverageLuminance);
		writer.NewLine();

		writer.Text(TEXT("Auto Exposure: "));
		writer.Float(exposure);
		writer.NewLine();
#endif
	}
}
