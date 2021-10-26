#include "CommonBindings.hlsli"
#include "Random.hlsli"

#define SSAO_SAMPLES 64
#define BLOCK_SIZE 16

#define RootSig ROOT_SIG("CBV(b0, visibility=SHADER_VISIBILITY_ALL), " \
				"DescriptorTable(UAV(u0, numDescriptors = 1), visibility=SHADER_VISIBILITY_ALL), " \
				"DescriptorTable(SRV(t0, numDescriptors = 1), visibility=SHADER_VISIBILITY_ALL)")

struct ShaderParameters
{
    float4x4 ProjectionInverse;
    float4x4 ViewInverse;
    float4x4 Projection;
    float4x4 View;
    uint2 Dimensions;
    float Near;
    float Far;
    float AoPower;
    float AoRadius;
    float AoDepthThreshold;
    int AoSamples;
    uint FrameIndex;
};

ConstantBuffer<ShaderParameters> cData : register(b0);
Texture2D tDepthTexture : register(t0);

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
    float2 dimInv = rcp((float2)cData.Dimensions);
    float2 texCoord = (float2)input.DispatchThreadId.xy * dimInv;
    float depth = tDepthTexture.SampleLevel(sLinearClamp, texCoord, 0).r;
    float3 normal = NormalFromDepth(tDepthTexture, sLinearClamp, texCoord, dimInv, cData.ProjectionInverse);
    float3 viewPos = ViewFromDepth(texCoord.xy, depth, cData.ProjectionInverse).xyz;

    uint state = SeedThread(input.DispatchThreadId.xy, cData.Dimensions, cData.FrameIndex);
	float3 randomVec = float3(Random01(state), Random01(state), Random01(state)) * 2.0f - 1.0f;
	float3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
	float3 bitangent = cross(tangent, normal);
	float3x3 TBN = float3x3(tangent, bitangent, normal);

    float occlusion = 0;

    for(int i = 0; i < cData.AoSamples; ++i)
    {
        float2 point2d = HammersleyPoints(i, cData.AoSamples);
		float3 hemispherePoint = HemisphereSampleUniform(point2d.x, point2d.y);
        float3 vpos = viewPos + mul(hemispherePoint, TBN) * cData.AoRadius;
        float4 newTexCoord = mul(float4(vpos, 1), cData.Projection);
        newTexCoord.xyz /= newTexCoord.w;
        newTexCoord.xy = newTexCoord.xy * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);
        if(newTexCoord.x >= 0 && newTexCoord.x <= 1 && newTexCoord.y >= 0 && newTexCoord.y <= 1)
        {
            float sampleDepth = tDepthTexture.SampleLevel(sLinearClamp, newTexCoord.xy, 0).r;
            float depthVpos = LinearizeDepth(sampleDepth, cData.Near, cData.Far);
            float rangeCheck = smoothstep(0.0f, 1.0f, cData.AoRadius / (viewPos.z - depthVpos));
            occlusion += (vpos.z >= depthVpos + cData.AoDepthThreshold) * rangeCheck;
        }
    }
    occlusion = occlusion / cData.AoSamples;
    uAmbientOcclusion[input.DispatchThreadId.xy] = pow(saturate(1 - occlusion), cData.AoPower);
}
