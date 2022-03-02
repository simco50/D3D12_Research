#include "CommonBindings.hlsli"
#include "Random.hlsli"

#define SSAO_SAMPLES 64
#define BLOCK_SIZE 16

struct PassData
{
	uint2 Dimensions;
	float AoPower;
	float AoRadius;
	float AoDepthThreshold;
	int AoSamples;
};

ConstantBuffer<PassData> cPass : register(b0);
Texture2D tDepthTexture : register(t0);

RWTexture2D<float> uAmbientOcclusion : register(u0);

struct CS_INPUT
{
	uint3 GroupId : SV_GroupID;
	uint3 GroupThreadId : SV_GroupThreadID;
	uint3 DispatchThreadId : SV_DispatchThreadID;
	uint GroupIndex : SV_GroupIndex;
};

[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void CSMain(CS_INPUT input)
{
	float2 dimInv = rcp((float2)cPass.Dimensions);
	float2 uv = (float2)input.DispatchThreadId.xy * dimInv;
	float depth = tDepthTexture.SampleLevel(sLinearClamp, uv, 0).r;
	float3 normal = NormalFromDepth(tDepthTexture, sLinearClamp, uv, dimInv, cView.ProjectionInverse);
	float3 viewPos = ViewFromDepth(uv.xy, depth, cView.ProjectionInverse).xyz;

	uint state = SeedThread(input.DispatchThreadId.xy, cPass.Dimensions, cView.FrameIndex);
	float3 randomVec = float3(Random01(state), Random01(state), Random01(state)) * 2.0f - 1.0f;
	float3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
	float3 bitangent = cross(tangent, normal);
	float3x3 TBN = float3x3(tangent, bitangent, normal);

	float occlusion = 0;

	for(int i = 0; i < cPass.AoSamples; ++i)
	{
		float2 point2d = HammersleyPoints(i, cPass.AoSamples);
		float3 hemispherePoint = HemisphereSampleUniform(point2d.x, point2d.y);
		float3 vpos = viewPos + mul(hemispherePoint, TBN) * cPass.AoRadius;
		float4 newTexCoord = mul(float4(vpos, 1), cView.Projection);
		newTexCoord.xyz /= newTexCoord.w;
		newTexCoord.xy = newTexCoord.xy * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);
		if(newTexCoord.x >= 0 && newTexCoord.x <= 1 && newTexCoord.y >= 0 && newTexCoord.y <= 1)
		{
			float sampleDepth = tDepthTexture.SampleLevel(sLinearClamp, newTexCoord.xy, 0).r;
			float depthVpos = LinearizeDepth(sampleDepth, cView.NearZ, cView.FarZ);
			float rangeCheck = smoothstep(0.0f, 1.0f, cPass.AoRadius / (viewPos.z - depthVpos));
			occlusion += (vpos.z >= depthVpos + cPass.AoDepthThreshold) * rangeCheck;
		}
	}
	occlusion = occlusion / cPass.AoSamples;
	uAmbientOcclusion[input.DispatchThreadId.xy] = pow(saturate(1 - occlusion), cPass.AoPower);
}
