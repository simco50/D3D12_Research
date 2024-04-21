#include "Common.hlsli"
#include "Random.hlsli"

#define BLOCK_SIZE 16

struct PassData
{
	float AoPower;
	float AoRadius;
	float AoDepthThreshold;
	int AoSamples;
};

ConstantBuffer<PassData> cPass : register(b0);
Texture2D<float> tDepthTexture : register(t0);

RWTexture2D<float> uAmbientOcclusion : register(u0);

float3x3 TangentMatrix(float3 z)
{
    float3 ref = abs(dot(z, float3(0, 1, 0))) > 0.99f ? float3(0, 0, 1) : float3(0, 1, 0);
    float3 x = normalize(cross(ref, z));
    float3 y = cross(z, x);
    return float3x3(x, y, z);
}

[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void CSMain(uint3 threadId : SV_DispatchThreadID)
{
	float2 uv = ((float2)threadId.xy + 0.5f) * cView.TargetDimensionsInv;
	float depth = tDepthTexture.SampleLevel(sPointClamp, uv, 0);
	float3 viewNormal = ViewNormalFromDepth(uv, tDepthTexture, NormalReconstructMethod::Taps5);
	float3 viewPos = ViewPositionFromDepth(uv.xy, depth, cView.ProjectionInverse).xyz;

	uint seed = SeedThread(threadId.xy, cView.TargetDimensions, cView.FrameIndex);
	float3 randomVec = float3(Random01(seed), Random01(seed), Random01(seed)) * 2.0f - 1.0f;
	float3x3 TBN = TangentMatrix(viewNormal);

	// Diffuse reflections integral is over (1 / PI) * Li * NdotL
	// We sample a cosine weighted distribution over the hemisphere which has a PDF which conveniently cancels out the inverse PI and NdotL terms.

	float occlusion = 0;

	for(int i = 0; i < cPass.AoSamples; ++i)
	{
		float2 u = float2(Random01(seed), Random01(seed));
		float pdf;
		float3 hemispherePoint = HemisphereSampleCosineWeight(u, pdf);
		float3 vpos = viewPos + mul(hemispherePoint, TBN) * cPass.AoRadius;
		float4 newTexCoord = mul(float4(vpos, 1), cView.Projection);
		newTexCoord.xyz /= newTexCoord.w;
		newTexCoord.xy = newTexCoord.xy * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);

		// Make sure we're not sampling outside the screen
		if(all(newTexCoord.xy >= 0) && all(newTexCoord.xy <= 1))
		{
			float sampleDepth = tDepthTexture.SampleLevel(sPointClamp, newTexCoord.xy, 0).r;
			float depthVpos = LinearizeDepth(sampleDepth);
			float rangeCheck = smoothstep(0.0f, 1.0f, cPass.AoRadius / (viewPos.z - depthVpos));
			occlusion += (vpos.z >= depthVpos + cPass.AoDepthThreshold) * rangeCheck;
		}
	}
	occlusion = occlusion / cPass.AoSamples;
	uAmbientOcclusion[threadId.xy] = pow(saturate(1 - occlusion), cPass.AoPower);
}
