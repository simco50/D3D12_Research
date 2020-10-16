#include "Common.hlsli"
#include "RNG.hlsli"

#define SSAO_SAMPLES 64
#define BLOCK_SIZE 16

#define RootSig "CBV(b0, visibility=SHADER_VISIBILITY_ALL), " \
				"DescriptorTable(UAV(u0, numDescriptors = 1), visibility=SHADER_VISIBILITY_ALL), " \
				"DescriptorTable(SRV(t0, numDescriptors = 1), visibility=SHADER_VISIBILITY_ALL), " \
				"StaticSampler(s0, filter=FILTER_MIN_MAG_LINEAR_MIP_POINT, visibility = SHADER_VISIBILITY_ALL), " \

cbuffer ShaderParameters : register(b0)
{
    float4x4 cProjectionInverse;
    float4x4 cViewInverse;
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
SamplerState sSampler : register(s0);

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
    float2 dimInv = rcp(cDimensions);
    float2 texCoord = (float2)input.DispatchThreadId.xy * dimInv;
    float depth = tDepthTexture.SampleLevel(sSampler, texCoord, 0).r;

    float3 viewPos = ViewFromDepth(texCoord.xy, depth, cProjectionInverse).xyz;
    float2 texCoord1 = texCoord + float2(dimInv.x, 0);
    float2 texCoord2 = texCoord + float2(0, -dimInv.y);
	float3 p1 = ViewFromDepth(texCoord1, tDepthTexture.SampleLevel(sSampler, texCoord1, 0).r, cProjectionInverse).xyz;
	float3 p2 = ViewFromDepth(texCoord2, tDepthTexture.SampleLevel(sSampler, texCoord2, 0).r, cProjectionInverse).xyz;
    float3 normal = normalize(cross(p2 - viewPos, p1 - viewPos));

    uint state = SeedThread(input.DispatchThreadId.x + input.DispatchThreadId.y * cDimensions.x);
	float3 randomVec = float3(Random01(state), Random01(state), Random01(state)) * 2.0f - 1.0f;
	float3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
	float3 bitangent = cross(tangent, normal);
	float3x3 TBN = float3x3(tangent, bitangent, normal);

    float occlusion = 0;
    
    for(int i = 0; i < cAoSamples; ++i)
    {
        float2 point2d = HammersleyPoints(i, cAoSamples);
		float3 hemispherePoint = HemisphereSampleUniform(point2d.x, point2d.y);
        float3 vpos = viewPos + mul(hemispherePoint, TBN) * cAoRadius;
        float4 newTexCoord = mul(float4(vpos, 1), cProjection);
        newTexCoord.xyz /= newTexCoord.w;
        newTexCoord.xy = newTexCoord.xy * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);
        if(newTexCoord.x >= 0 && newTexCoord.x <= 1 && newTexCoord.y >= 0 && newTexCoord.y <= 1)
        {
            float sampleDepth = tDepthTexture.SampleLevel(sSampler, newTexCoord.xy, 0).r;
            float depthVpos = LinearizeDepth(sampleDepth, cNear, cFar);
            float rangeCheck = smoothstep(0.0f, 1.0f, cAoRadius / (viewPos.z - depthVpos));
            occlusion += (vpos.z >= depthVpos + cAoDepthThreshold) * rangeCheck;
        }
    }
    occlusion = occlusion / cAoSamples;
    uAmbientOcclusion[input.DispatchThreadId.xy] = pow(saturate(1 - occlusion), cAoPower);
}