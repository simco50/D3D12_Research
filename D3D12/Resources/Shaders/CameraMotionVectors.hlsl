#include "Common.hlsli"

Texture2D tDepthTexture : register(t0);
RWTexture2D<float2> uVelocity  : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 threadID : SV_DispatchThreadID)
{
	float2 uv = (threadID.xy + 0.5f) * cView.TargetDimensionsInv;
	float2 clipCurr = uv * float2(2, -2) + float2(-1, 1);
	float depth = tDepthTexture[threadID.xy].r;
	float4 currWorldPos = mul(float4(clipCurr, depth, 1.0f), cView.ViewProjectionInverse);
	currWorldPos.xyz /= currWorldPos.w;
	float4 clipPrev = mul(float4(currWorldPos.xyz, 1), cView.ViewProjectionPrev);
	clipPrev.xy /= clipPrev.w;
	float2 velocity = ((clipPrev.xy - cView.ViewJitterPrev) - (clipCurr - cView.ViewJitter)) * float2(0.5f, -0.5f);
	uVelocity[threadID.xy] = velocity;
}
