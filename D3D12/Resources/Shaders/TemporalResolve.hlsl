#include "TonemappingCommon.hlsli"

#define RootSig "CBV(b0, visibility=SHADER_VISIBILITY_ALL), " \
                "DescriptorTable(UAV(u0, numDescriptors = 1), visibility=SHADER_VISIBILITY_ALL), " \
				"DescriptorTable(SRV(t0, numDescriptors = 4), visibility=SHADER_VISIBILITY_ALL), " \
				"StaticSampler(s0, filter=FILTER_MIN_MAG_MIP_POINT, visibility = SHADER_VISIBILITY_ALL, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP), " \
				"StaticSampler(s1, filter=FILTER_MIN_MAG_MIP_LINEAR, visibility = SHADER_VISIBILITY_ALL, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP), " \

struct ShaderParameters
{
    float4x4 Reprojection;
    float2 InvScreenDimensions;
    float2 Jitter;
};

ConstantBuffer<ShaderParameters> cParameters : register(b0);

SamplerState sPointSampler : register(s0);
SamplerState sLinearSampler : register(s1);

Texture2D tVelocity : register(t0);
Texture2D tPreviousColor : register(t1);
Texture2D tCurrentColor : register(t2);
Texture2D tDepth : register(t3);
RWTexture2D<float4> uInOutColor : register(u0);

//Temporal Reprojection in Inside
float4 ClipAABB(float3 aabb_min, float3 aabb_max, float4 p, float4 q)
{
    // note: only clips towards aabb center (but fast!)
    float3 p_clip = 0.5 * (aabb_max + aabb_min);
    float3 e_clip = 0.5 * (aabb_max - aabb_min) + 0.00000001f;

    float4 v_clip = q - float4(p_clip, p.w);
    float3 v_unit = v_clip.xyz / e_clip;
    float3 a_unit = abs(v_unit);
    float ma_unit = max(a_unit.x, max(a_unit.y, a_unit.z));

    if (ma_unit > 1.0)
        return float4(p_clip, p.w) + v_clip / ma_unit;
    else
        return q;// point inside aabb
}

[RootSignature(RootSig)]
[numthreads(8, 8, 1)]
void CSMain(uint3 ThreadId : SV_DISPATCHTHREADID)
{
    uint2 pixelIndex = ThreadId.xy;
    float2 texCoord = cParameters.InvScreenDimensions * ((float2)pixelIndex + 0.5f);

    float3 neighborhood[9];
    int index = 0;
    [unroll]
    for(int x = -1; x <= 1; ++x)
    {
        [unroll]
        for(int y = -1; y <= 1; ++y)
        {
            float3 color = tCurrentColor.SampleLevel(sPointSampler, texCoord + cParameters.InvScreenDimensions * float2(x, y), 0).rgb;
            neighborhood[index++] = color;
        }
    }
    
    float3 aabb_min = 1000000000;
    float3 aabb_max = 0;
    for(int i = 0; i < 9; ++i)
    {
        aabb_min = min(aabb_min, neighborhood[i]);
        aabb_max = max(aabb_max, neighborhood[i]);
    }

    float3 currColor = tCurrentColor[pixelIndex].rgb;

    float depth = tDepth[pixelIndex].r;
    float4 pos = float4(texCoord, depth, 1);
    float4 prevPos = mul(pos, cParameters.Reprojection);
    prevPos.xyz /= prevPos.w;
    float2 velocity = (prevPos - pos).xy / 2;

    texCoord += velocity;

    float3 prevColor = tPreviousColor.SampleLevel(sLinearSampler, texCoord, 0).rgb;
    prevColor = clamp(prevColor, aabb_min, aabb_max);

    float blend = 0.05f;
    float2 blendA = texCoord > float2(1, 1);
    float2 blendB = texCoord < float2(0, 0);
    if(texCoord.x < 0 || texCoord.x > 1 || texCoord.y < 0 || texCoord.y > 1)
    {
        blend = 1;
    }
    currColor = lerp(prevColor, currColor, blend);
    uInOutColor[pixelIndex] = float4(currColor, 1);
}