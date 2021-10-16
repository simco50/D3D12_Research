#include "CommonBindings.hlsli"

#define RootSig ROOT_SIG("CBV(b0, visibility=SHADER_VISIBILITY_ALL), " \
				"DescriptorTable(UAV(u0, numDescriptors = 1), visibility = SHADER_VISIBILITY_ALL), " \
				"DescriptorTable(SRV(t0, numDescriptors = 1), visibility = SHADER_VISIBILITY_ALL)")

#define BLOCK_SIZE 8

#define TEXTURE_STORAGE float4
#define TEXTURE_INPUT_TYPE Texture2D<TEXTURE_STORAGE>
#define TEXTURE_OUTPUT_TYPE RWTexture2D<TEXTURE_STORAGE>

TEXTURE_OUTPUT_TYPE uOutput : register(u0);
TEXTURE_INPUT_TYPE tInput : register(t0);

cbuffer ShaderParameters : register(b0)
{
    uint2 cTargetDimensions;
    float2 cTargetDimensionsInv;
}

struct CS_INPUT
{
    uint3 DispatchThreadId : SV_DISPATCHTHREADID;
};

[RootSignature(RootSig)]
[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void CSMain(CS_INPUT input)
{
    if(input.DispatchThreadId.x < cTargetDimensions.x && input.DispatchThreadId.y < cTargetDimensions.y)
    {
        uOutput[input.DispatchThreadId.xy] = tInput.SampleLevel(sLinearClamp, ((float2)input.DispatchThreadId.xy + 0.5f) * cTargetDimensionsInv, 0);
    }
}