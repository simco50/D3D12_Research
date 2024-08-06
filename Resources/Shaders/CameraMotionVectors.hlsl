#include "Common.hlsli"

Texture2D tDepthTexture : register(t0);
RWTexture2D<float2> uVelocity  : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 threadID : SV_DispatchThreadID)
{
	if(any(threadID.xy >= cView.ViewportDimensions))
		return;

	float depth = tDepthTexture[threadID.xy].r;

	float2 posSS = threadID.xy + 0.5f;
	float2 uv = posSS * cView.ViewportDimensionsInv;

	// Compute world space position from depth
	float4 posNDC = float4(UVToClip(uv), depth, 1.0f);
	float4 posWS = mul(posNDC, cView.ClipToWorld);
	posWS /= posWS.w;

	// Project into last frame's view
	float4 prevPosNDC = mul(float4(posWS.xyz, 1), cView.WorldToClipPrev);
	prevPosNDC /= prevPosNDC.w;

	// Velocity is the _from_ current view _to_ last  view
	float2 velocity = (prevPosNDC.xy - posNDC.xy);
	
	// Remove jitter
	velocity += cView.ViewJitter - cView.ViewJitterPrev;
	
	// NDC to UV offset
	uVelocity[threadID.xy] = velocity * float2(0.5f, -0.5f);
}
