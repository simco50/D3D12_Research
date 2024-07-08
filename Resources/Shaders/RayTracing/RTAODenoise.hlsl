#include "Common.hlsli"

RWTexture2D<float> uOutput : register(u0);
Texture2D tSceneDepth : register(t0);
Texture2D<float> tHistory : register(t1);
Texture2D<float> tAO : register(t2);
Texture2D<float2> tVelocity : register(t3);

[numthreads(8, 8, 1)]
void DenoiseCS(uint3 threadID : SV_DispatchThreadID)
{
	if(any(threadID.xy >= cView.ViewportDimensions))
		return;

	float ao = tAO[threadID.xy];
	float2 uv = (threadID.xy + 0.5f) * cView.ViewportDimensionsInv;
	float2 velocity = tVelocity.SampleLevel(sLinearClamp, uv, 0);
	float2 reprojUV = uv + velocity;
	float prevAO = tHistory.SampleLevel(sLinearClamp, reprojUV, 0);

	float historyWeight = 0.95f;
	if(any(reprojUV < 0) || any(reprojUV > 1))
	{
		historyWeight = 0.0f;
	}
	uOutput[threadID.xy] = lerp(ao, prevAO, historyWeight);
}
