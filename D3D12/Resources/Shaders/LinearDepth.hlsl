#include "Common.hlsli"

#define RootSig "CBV(b0, visibility=SHADER_VISIBILITY_ALL), " \
				"DescriptorTable(UAV(u0, numDescriptors = 1), visibility = SHADER_VISIBILITY_ALL), " \
				"DescriptorTable(SRV(t0, numDescriptors = 1), visibility = SHADER_VISIBILITY_ALL), " \

#define BLOCK_SIZE 16

RWTexture2D<float> uOutput : register(u0);
Texture2D<float> tInput : register(t0);

cbuffer ShaderParameters : register(b0)
{
    float cNear;
    float cFar;
}

struct CS_INPUT
{
    uint3 DispatchThreadId : SV_DISPATCHTHREADID;
};

float LinearizeDepth(float depth)
{
    return cNear * cFar / (cFar + depth * (cNear - cFar));
}

[RootSignature(RootSig)]
[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void CSMain(CS_INPUT input)
{
    uint width, height;
    uOutput.GetDimensions(width, height);
    if(input.DispatchThreadId.x < width && input.DispatchThreadId.y < height)
    {
        uOutput[input.DispatchThreadId.xy] = LinearizeDepth(tInput[input.DispatchThreadId.xy]);
    }
}