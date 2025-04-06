#include "Common.hlsli"

struct PassParams
{
	Texture2DH<float> DepthTexture;
	RWTexture2DH<float2> Velocity;
};
DEFINE_CONSTANTS(PassParams, 0);

[numthreads(8, 8, 1)]
void CSMain(uint3 threadID : SV_DispatchThreadID)
{
	if(any(threadID.xy >= cView.ViewportDimensions))
		return;

	float depth = cPassParams.DepthTexture[threadID.xy];
	float2 uv = TexelToUV(threadID.xy, cView.ViewportDimensionsInv);

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
	cPassParams.Velocity.Store(threadID.xy, velocity * float2(0.5f, -0.5f));
}
