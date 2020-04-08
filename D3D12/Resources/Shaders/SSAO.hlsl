#include "Common.hlsli"

#define SSAO_SAMPLES 64
#define BLOCK_SIZE 16

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
    float cNear;
    float cFar;
    float cAoPower;
    float cAoRadius;
    float cAoDepthThreshold;
    int cAoSamples;
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

    float3 randomVec = normalize(float3(tNoiseTexture.SampleLevel(sPointSampler, texCoord * (float2)cDimensions / 1000, 0).xy, 0));
	float3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
	float3 bitangent = cross(tangent, normal);
	float3x3 TBN = float3x3(tangent, bitangent, normal);

    float occlusion = 0;
    
    for(int i = 0; i < cAoSamples; ++i)
    {
        float3 vpos = viewPos.xyz + mul(cRandomVectors[i].xyz, TBN) * cAoRadius;
        float4 newTexCoord = mul(float4(vpos, 1), cProjection);
        newTexCoord.xyz /= newTexCoord.w;
        newTexCoord.xy = newTexCoord.xy * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);
        if(newTexCoord.x >= 0 && newTexCoord.x <= 1 && newTexCoord.y >= 0 && newTexCoord.y <= 1)
        {
            float sampleDepth = tDepthTexture.SampleLevel(sSampler, newTexCoord.xy, 0).r;
            float4 depthVpos = ScreenToView(float4(newTexCoord.xy, sampleDepth, 1), float2(1, 1), cProjectionInverse);
            float rangeCheck = smoothstep(0.0f, 1.0f, cAoRadius / (viewPos.z - depthVpos.z));
            occlusion += (vpos.z >= depthVpos.z + cAoDepthThreshold) * rangeCheck;
        }
    }
    occlusion = occlusion / cAoSamples;
    uAmbientOcclusion[input.DispatchThreadId.xy] = pow(saturate(1 - occlusion), cAoPower);
}