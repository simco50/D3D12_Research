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
    float2 dxdy = cInvScreenDimensions;
    
    float3 neighborhood[9];
    neighborhood[0] = tCurrentColor.SampleLevel(sDefaultSampler, texCoord + dxdy * float2(-1, -1), 0).rgb;
    neighborhood[1] = tCurrentColor.SampleLevel(sDefaultSampler, texCoord + dxdy * float2(0, -1), 0).rgb;
    neighborhood[2] = tCurrentColor.SampleLevel(sDefaultSampler, texCoord + dxdy * float2(1, -1), 0).rgb;
    neighborhood[3] = tCurrentColor.SampleLevel(sDefaultSampler, texCoord + dxdy * float2(-1, 0), 0).rgb;
    neighborhood[4] = tCurrentColor.SampleLevel(sDefaultSampler, texCoord + dxdy * float2(0, 0), 0).rgb;
    neighborhood[5] = tCurrentColor.SampleLevel(sDefaultSampler, texCoord + dxdy * float2(1, 0), 0).rgb;
    neighborhood[6] = tCurrentColor.SampleLevel(sDefaultSampler, texCoord + dxdy * float2(-1, 1), 0).rgb;
    neighborhood[7] = tCurrentColor.SampleLevel(sDefaultSampler, texCoord + dxdy * float2(0, 1), 0).rgb;
    neighborhood[8] = tCurrentColor.SampleLevel(sDefaultSampler, texCoord + dxdy * float2(1, 1), 0).rgb;
    
    float3 minn = 1000000000;
    float3 maxx = 0;
    for(int i = 0; i < 9; ++i)
    {
        minn = min(minn, neighborhood[i]);
        maxx = max(maxx, neighborhood[i]);
    }

    float2 v = tVelocity.SampleLevel(sDefaultSampler, texCoord, 0).rg * cInvScreenDimensions;
    texCoord -= v;

    float3 a = neighborhood[4];
    float3 b = clamp(tPreviousColor.SampleLevel(sDefaultSampler, texCoord, 0).rgb, minn, maxx);
    
    float2 blendA = texCoord > float2(1, 1);
    float2 blendB = texCoord < float2(0, 0);
    float blend = 0.05;
    if(texCoord.x < 0 || texCoord.x > 1 || texCoord.y < 0 || texCoord.y > 1)
    {
        blend = 1;
    }
    uInOutColor[input.DispatchThreadId.xy] = float4(lerp(b, a, blend), 1);
}