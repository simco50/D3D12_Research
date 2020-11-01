#define RootSig "CBV(b0, visibility=SHADER_VISIBILITY_ALL), " \
                "DescriptorTable(UAV(u0, numDescriptors = 1), visibility=SHADER_VISIBILITY_ALL), " \
				"DescriptorTable(SRV(t0, numDescriptors = 3), visibility=SHADER_VISIBILITY_ALL), " \
				"StaticSampler(s0, filter=FILTER_MIN_MAG_MIP_LINEAR, visibility = SHADER_VISIBILITY_ALL, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP), " \

cbuffer Parameters : register(b0)
{
    float2 cInvScreenDimensions;
}

SamplerState sDefaultSampler : register(s0);

Texture2D tVelocity : register(t0);
Texture2D tPreviousColor : register(t1);
Texture2D tCurrentColor : register(t2);
RWTexture2D<float4> uInOutColor : register(u0);

struct CS_INPUT
{
    uint3 DispatchThreadId : SV_DISPATCHTHREADID;
};

[RootSignature(RootSig)]
[numthreads(8, 8, 1)]
void CSMain(CS_INPUT input)
{
    float2 texCoord = cInvScreenDimensions * ((float2)input.DispatchThreadId.xy + 0.5f);
    float4 a = tCurrentColor.SampleLevel(sDefaultSampler, texCoord, 0);
    float2 v = tVelocity.SampleLevel(sDefaultSampler, texCoord, 0).rg;
    float4 b = tPreviousColor.SampleLevel(sDefaultSampler, texCoord - v * cInvScreenDimensions, 0);
    uInOutColor[input.DispatchThreadId.xy] = lerp(a, b, 0.95f);
}