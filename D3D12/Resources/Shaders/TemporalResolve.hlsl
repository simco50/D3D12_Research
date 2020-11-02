#include "TonemappingCommon.hlsli"

#define RootSig "CBV(b0, visibility=SHADER_VISIBILITY_ALL), " \
                "DescriptorTable(UAV(u0, numDescriptors = 1), visibility=SHADER_VISIBILITY_ALL), " \
				"DescriptorTable(SRV(t0, numDescriptors = 4), visibility=SHADER_VISIBILITY_ALL), " \
				"StaticSampler(s0, filter=FILTER_MIN_MAG_MIP_POINT, visibility = SHADER_VISIBILITY_ALL, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP), " \
				"StaticSampler(s1, filter=FILTER_MIN_MAG_MIP_LINEAR, visibility = SHADER_VISIBILITY_ALL, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP), " \

cbuffer Parameters : register(b0)
{
    float2 cInvScreenDimensions;
    float2 cJitter;
}

SamplerState sPointSampler : register(s0);
SamplerState sLinearSampler : register(s1);

Texture2D tVelocity : register(t0);
Texture2D tPreviousColor : register(t1);
Texture2D tCurrentColor : register(t2);
Texture2D tDepth : register(t3);
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
    float2 dimensions;
    tCurrentColor.GetDimensions(dimensions.x, dimensions.y);

    float2 dxdy = cInvScreenDimensions;
    
    float3 neighborhood[9];

    int index = 0;
    [unroll]
    for(int x = -1; x <= 1; ++x)
    {
        [unroll]
        for(int y = -1; y <= 1; ++y)
        {
            neighborhood[index++] = tCurrentColor.SampleLevel(sPointSampler, texCoord + dxdy * float2(x, y), 0).rgb;
        }
    }
    
    float3 minn = 1000000000;
    float3 maxx = 0;
    for(int i = 0; i < 9; ++i)
    {
        minn = min(minn, neighborhood[i]);
        maxx = max(maxx, neighborhood[i]);
    }
    float3 currColor = neighborhood[4];

    float2 velocity = tVelocity.SampleLevel(sPointSampler, texCoord, 0).rg;
    texCoord -= velocity;

    float3 prevColor = clamp(tPreviousColor.SampleLevel(sLinearSampler, texCoord, 0).rgb, minn, maxx);
    float correction = frac(max(abs(velocity.x) * dimensions.y, abs(velocity.y) * dimensions.y)) * 0.5f;

    float blend = saturate(lerp(0.05f, 0.8f, correction));
    float2 blendA = texCoord > float2(1, 1);
    float2 blendB = texCoord < float2(0, 0);
    if(texCoord.x < 0 || texCoord.x > 1 || texCoord.y < 0 || texCoord.y > 1)
    {
        blend = 1;
    }
    uInOutColor[input.DispatchThreadId.xy] = float4(lerp(prevColor, currColor, blend), 1);
}