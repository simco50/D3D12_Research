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
    float4x4 cView;
    uint2 cDimensions;
    float cNear;
    float cFar;
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

    float4 viewPos = ScreenToView(float4(texCoord.xy, fDepth, 1), (float2)cDimensions, cProjectionInverse);
    float3 normal = normalize(mul(tNormalsTexture[texCoord].xyz, (float3x3)cView));

    float3 randomVec = normalize(float3(0.23f, 0.48f, 0));
	float3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
	float3 bitangent = cross(tangent, normal);
	float3x3 TBN = float3x3(tangent, bitangent, normal);

    float occlusion = 0;
    int kernelSize = 64;

    float radius = 1.5;
    for(int i = 0; i < kernelSize; ++i)
    {
        float3 newViewPos = viewPos.xyz + mul(cRandomVectors[i].xyz, TBN) * radius;
        float4 newTexCoord = mul(float4(newViewPos, 1), cProjection);
        newTexCoord.xyz /= newTexCoord.w;
        newTexCoord.xyz = newTexCoord.xyz * 0.5f + 0.5f;
        newTexCoord.y = 1 - newTexCoord.y;
        float newDepth = tDepthTexture[newTexCoord.xy * cDimensions].r;
        float4 viewPos2 = ScreenToView(float4(newTexCoord.xy, newDepth, 1), (float2)cDimensions, cProjectionInverse);
        float rangeCheck = smoothstep(0.0, 1.0, radius / abs(viewPos.z - viewPos2.z));
        occlusion += rangeCheck * (newViewPos.z >= viewPos2.z + 0.25f ? 1 : 0);
    }
    occlusion = 1.0 - (occlusion / kernelSize);
    occlusion = pow(occlusion, 4);
    uAmbientOcclusion[texCoord] = float4(occlusion, occlusion, occlusion, 1);
}