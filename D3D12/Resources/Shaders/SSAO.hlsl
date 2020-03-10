#include "Common.hlsli"

#define RootSig "CBV(b0, visibility=SHADER_VISIBILITY_ALL), " \
				"DescriptorTable(UAV(u0, numDescriptors = 1), visibility=SHADER_VISIBILITY_ALL), " \
				"DescriptorTable(SRV(t0, numDescriptors = 2), visibility=SHADER_VISIBILITY_ALL), " \

cbuffer ShaderParameters : register(b0)
{
    float4 cRandomVectors[64];
    float4x4 cViewInverse;
    float4x4 cProjectionInverse;
    float4x4 cProjection;
    uint2 cDimensions;
}

Texture2D tDepthTexture : register(t0);
Texture2D tNormalsTexture : register(t1);

RWTexture2D<float4> uAmbientOcclusion : register(u0);

struct CS_INPUT
{
    uint3 GroupId : SV_GROUPID;
    uint3 GroupThreadId : SV_GROUPTHREADID;
    uint3 DispatchThreadId : SV_DISPATCHTHREADID;
    uint GroupIndex : SV_GROUPINDEX;
};

[RootSignature(RootSig)]
[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void CSMain(CS_INPUT input)
{
    uint2 texCoord = (float2)input.DispatchThreadId.xy;
    float fDepth = tDepthTexture[texCoord].r;

    float3 viewPos = ScreenToView(float4(texCoord.xy, fDepth, 1), (float2)cDimensions, cProjectionInverse).xyz;
    float3 normal = tNormalsTexture[texCoord].xyz;

    float occlusion = 0;
    int kernelSize = 64;
    for(int i = 0; i < kernelSize; ++i)
    {
        float3 newViewPos = viewPos + cRandomVectors[i].xyz * 0.5f;
        float4 newTexCoord = mul(float4(newViewPos, 1), cProjection);
        newTexCoord.xyz /= newTexCoord.w;
        newTexCoord.xyz = newTexCoord.xyz * 0.5f + 0.5f;
        newTexCoord.y = 1 - newTexCoord.y;
        float newDepth = tDepthTexture[newTexCoord.xy * cDimensions].r;
        occlusion += (newDepth >= fDepth + 0.025f ? 1 : 0);
    }
    occlusion = 1.0 - (occlusion / kernelSize);

    uAmbientOcclusion[texCoord] = float4(occlusion, occlusion, occlusion, 1);
}