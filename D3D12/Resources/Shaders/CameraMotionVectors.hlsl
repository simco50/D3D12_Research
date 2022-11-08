#include "Common.hlsli"

Texture2D tDepthTexture : register(t0);
RWTexture2D<float2> uVelocity  : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 threadID : SV_DispatchThreadID)
{
	float2 uv = cView.TargetDimensionsInv * (threadID.xy + 0.5f);
	float depth = tDepthTexture[threadID.xy].r;
	float4 pos = float4(uv, depth, 1);
	float4 prevPos = mul(pos, cView.ReprojectionMatrix);
	prevPos.xyz /= prevPos.w;
	uVelocity[threadID.xy] = (prevPos - pos).xy;
}
