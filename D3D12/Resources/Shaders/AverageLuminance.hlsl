#define RootSig "CBV(b0, visibility=SHADER_VISIBILITY_ALL), " \
				"DescriptorTable(UAV(u0, numDescriptors = 1))," \
				"DescriptorTable(SRV(t0, numDescriptors = 1))"

#define NUM_HISTOGRAM_BINS 256
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
        float weightedAverageLuminance = exp2(((weightedLogAverage / 254.0) * cLogLuminanceRange) + cMinLogLuminance);
        float luminanceLastFrame = uLuminanceOutput[0];
        float adaptedLuminance = luminanceLastFrame + (weightedAverageLuminance - luminanceLastFrame) * (1 - exp(-cTimeDelta * cTau));
        uLuminanceOutput[0] = adaptedLuminance;
        uLuminanceOutput[1] = weightedAverageLuminance;
    }
}