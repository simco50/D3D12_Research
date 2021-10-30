#include "CommonBindings.hlsli"

#define RootSig ROOT_SIG("CBV(b0), " \
				"DescriptorTable(UAV(u0, numDescriptors = 1)), " \
				"DescriptorTable(SRV(t0, numDescriptors = 1))")

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

[RootSignature(RootSig)]
[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void CSMain(CS_INPUT input)
{
    uint width, height;
    uOutput.GetDimensions(width, height);
    if(input.DispatchThreadId.x < width && input.DispatchThreadId.y < height)
    {
        uOutput[input.DispatchThreadId.xy] = LinearizeDepth01(tInput[input.DispatchThreadId.xy], cNear, cFar);
    }
}
