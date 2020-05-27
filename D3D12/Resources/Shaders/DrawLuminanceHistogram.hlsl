#define RootSig "CBV(b0, visibility=SHADER_VISIBILITY_ALL), " \
				"DescriptorTable(UAV(u0, numDescriptors = 1))," \
				"DescriptorTable(SRV(t0, numDescriptors = 2)), "

#define BIN_COUNT 256

#define BACKGROUND_COLOR float4(0, 0, 0, 0.6f)
#define FOREGROUND_COLOR float4(1, 0, 0, 0.8f)
#define CURSOR_COLOR float4(1, 0, 1, 1.0f)
#define TARGET_COLOR float4(1, 1, 1, 1.0f)

RWTexture2D<float4> uOutTexture : register(u0);
ByteAddressBuffer tLuminanceHistogram : register(t0);
StructuredBuffer<float> tAverageLuminance : register(t1);

cbuffer LuminanceHistogramBuffer : register(b0)
{
    float cMinLogLuminance;
    float cInverseLogLuminanceRange;
};

groupshared uint gsHistogram[BIN_COUNT];

void BlendPixel(uint2 location, uint2 offset, float4 color)
{
    uOutTexture[location + offset] = uOutTexture[location + offset] * (1 - color.a) + color * color.a;
}

[RootSignature(RootSig)]
[numthreads(BIN_COUNT, 1, 1)]
void DrawLuminanceHistogram(uint groupIndex : SV_GroupIndex, uint3 threadId : SV_DispatchThreadID)
{
    uint maxBinValue = 1;
    uint currentBinValue = tLuminanceHistogram.Load(groupIndex * 4);
    gsHistogram[groupIndex] = currentBinValue;
    GroupMemoryBarrierWithGroupSync();
    for(int i = 0; i < BIN_COUNT; ++i)
    {
        maxBinValue = max(maxBinValue, gsHistogram[i]);
    }
    uint c = gsHistogram[groupIndex];

    uint2 dimensions;
    uOutTexture.GetDimensions(dimensions.x, dimensions.y);
    uint2 s = uint2(dimensions.x, 0) + uint2(-BIN_COUNT * 4 + groupIndex * 4, threadId.y);

    float currentAverage = tAverageLuminance[0];
    float targetAverage = tAverageLuminance[1];
    float tCurrent = (log2(currentAverage) - cMinLogLuminance) * cInverseLogLuminanceRange;
    float tTarget = (log2(targetAverage) - cMinLogLuminance) * cInverseLogLuminanceRange;

    float4 outColor = BACKGROUND_COLOR;
    if(floor(tCurrent * BIN_COUNT) == groupIndex)
    {
        outColor = CURSOR_COLOR;
    }
    else if(floor(tTarget * BIN_COUNT) == groupIndex)
    {
        outColor = TARGET_COLOR;
    }
    else if(BIN_COUNT - threadId.y < (currentBinValue * BIN_COUNT) / maxBinValue)
    {
        outColor = FOREGROUND_COLOR;
    }
    BlendPixel(s, uint2(0, 0), BACKGROUND_COLOR);
    BlendPixel(s, uint2(1, 0), outColor);
    BlendPixel(s, uint2(2, 0), outColor);
    BlendPixel(s, uint2(3, 0), BACKGROUND_COLOR);
}