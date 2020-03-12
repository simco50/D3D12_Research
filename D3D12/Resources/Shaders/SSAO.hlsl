#include "Common.hlsli"

#define SSAO_SAMPLES 32

#define RootSig "CBV(b0, visibility=SHADER_VISIBILITY_ALL), " \
				"DescriptorTable(UAV(u0, numDescriptors = 1), visibility=SHADER_VISIBILITY_ALL), " \
				"DescriptorTable(SRV(t0, numDescriptors = 3), visibility=SHADER_VISIBILITY_ALL), " \
				"StaticSampler(s0, filter=FILTER_MIN_MAG_LINEAR_MIP_POINT, visibility = SHADER_VISIBILITY_ALL), " \
				"StaticSampler(s1, filter=FILTER_MIN_MAG_MIP_POINT, visibility = SHADER_VISIBILITY_ALL), " \

cbuffer ShaderParameters : register(b0)
{
    float4 cRandomVectors[SSAO_SAMPLES];
    float4x4 cProjectionInverse;
    float4x4 cProjection;
    float4x4 cView;
    uint2 cDimensions;
}

Texture2D tDepthTexture : register(t0);
Texture2D tNormalsTexture : register(t1);
Texture2D tNoiseTexture : register(t2);
SamplerState sSampler : register(s0);
SamplerState sPointSampler : register(s1);

RWTexture2D<float> uAmbientOcclusion : register(u0);

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
    float2 texCoord = (float2)input.DispatchThreadId.xy / cDimensions;
    float depth = tDepthTexture.SampleLevel(sSampler, texCoord, 0).r;

    float4 viewPos = ScreenToView(float4(texCoord.xy, depth, 1), float2(1, 1), cProjectionInverse);
    float3 normal = normalize(mul(tNormalsTexture.SampleLevel(sSampler, texCoord, 0).xyz, (float3x3)cView));

    float3 randomVec = normalize(float3(tNoiseTexture.SampleLevel(sPointSampler, texCoord * (float2)cDimensions / 100, 0).xy, 0));
	float3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
	float3 bitangent = cross(tangent, normal);
	float3x3 TBN = float3x3(tangent, bitangent, normal);

    float occlusion = 0;
    int kernelSize = SSAO_SAMPLES;
    float radius = 0.75;
    
    for(int i = 0; i < kernelSize; ++i)
    {
        float3 vpos = viewPos.xyz + mul(cRandomVectors[i].xyz, TBN) * radius;
        float4 newTexCoord = mul(float4(vpos, 1), cProjection);
        newTexCoord.xyz /= newTexCoord.w;
        newTexCoord.xyz = newTexCoord.xyz * 0.5f + 0.5f;
        newTexCoord.y = 1 - newTexCoord.y;
        float projectedDepth = tDepthTexture.SampleLevel(sSampler, newTexCoord.xy, 0).r;
        float4 depthVpos = ScreenToView(float4(newTexCoord.xy, projectedDepth, 1), float2(1, 1), cProjectionInverse);
        float rangeCheck = smoothstep(0.0, 1.0, radius / abs(viewPos.z - depthVpos.z));
        occlusion += rangeCheck * (vpos.z >= depthVpos.z + 0.025f ? 1 : 0);
    }
    occlusion = 1.0 - (occlusion / kernelSize);
    uAmbientOcclusion[input.DispatchThreadId.xy] = occlusion;
}