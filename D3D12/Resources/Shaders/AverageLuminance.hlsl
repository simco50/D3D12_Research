#include "CommonBindings.hlsli"
#include "TonemappingCommon.hlsli"

#define RootSig ROOT_SIG("CBV(b0, visibility=SHADER_VISIBILITY_ALL), " \
				"DescriptorTable(UAV(u0, numDescriptors = 1))," \
				"DescriptorTable(SRV(t0, numDescriptors = 1))")

#define HISTOGRAM_AVERAGE_THREADS_PER_DIMENSION 16

ByteAddressBuffer tLuminanceHistogram : register(t0);
RWStructuredBuffer<float> uLuminanceOutput : register(u0);

cbuffer LuminanceHistogramAverageBuffer : register(b0)
{
    uint cPixelCount;
    float cMinLogLuminance;
    float cLogLuminanceRange;
    float cTimeDelta;
    float cTau;
};

groupshared float gHistogramShared[NUM_HISTOGRAM_BINS];

struct CSInput
{
    uint groupIndex : SV_GroupIndex;
};

float Adaption(float current, float target, float dt, float speed)
{
    float factor = 1.0f - exp2(-dt * speed);
    return current + (target - current) * factor;
}

[RootSignature(RootSig)]
[numthreads(HISTOGRAM_AVERAGE_THREADS_PER_DIMENSION, HISTOGRAM_AVERAGE_THREADS_PER_DIMENSION, 1)]
void CSMain(CSInput input)
{
    float countForThisBin = (float)tLuminanceHistogram.Load(input.groupIndex * 4);
    gHistogramShared[input.groupIndex] = countForThisBin * (float)input.groupIndex;
    GroupMemoryBarrierWithGroupSync();

    [unroll]
    for(uint histogramSampleIndex = (NUM_HISTOGRAM_BINS >> 1); histogramSampleIndex > 0; histogramSampleIndex >>= 1)
    {
        if(input.groupIndex < histogramSampleIndex)
        {
            gHistogramShared[input.groupIndex] += gHistogramShared[input.groupIndex + histogramSampleIndex];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if(input.groupIndex == 0)
    {
        float weightedLogAverage = (gHistogramShared[0].x / max((float)cPixelCount - countForThisBin, 1.0)) - 1.0;
        float weightedAverageLuminance = exp2(((weightedLogAverage / (NUM_HISTOGRAM_BINS - 1)) * cLogLuminanceRange) + cMinLogLuminance);
        float luminanceLastFrame = uLuminanceOutput[0];
        float adaptedLuminance = Adaption(luminanceLastFrame, weightedAverageLuminance, cTimeDelta, cTau);

        uLuminanceOutput[0] = adaptedLuminance;
        uLuminanceOutput[1] = weightedAverageLuminance;
        uLuminanceOutput[2] = Exposure(EV100FromLuminance(adaptedLuminance));
    }
}
