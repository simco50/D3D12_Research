#include "Common.hlsli"

struct PassData
{
	RWTexture2DH<float> Output;
	Texture2DH<float> SceneDepth;
	Texture2DH<float> History;
	Texture2DH<float> AO;
	Texture2DH<float2> Velocity;
};
DEFINE_CONSTANTS(PassData, 0);

[numthreads(8, 8, 1)]
void DenoiseCS(uint3 threadID : SV_DispatchThreadID)
{
	if(any(threadID.xy >= cView.ViewportDimensions))
		return;

	float ao = cPassData.AO[threadID.xy];
	float2 uv = TexelToUV(threadID.xy, cView.ViewportDimensionsInv);
	float2 velocity = cPassData.Velocity.SampleLevel(sLinearClamp, uv, 0);
	float2 reprojUV = uv + velocity;
	float prevAO = cPassData.History.SampleLevel(sLinearClamp, reprojUV, 0);

	float historyWeight = 0.95f;
	if(any(reprojUV < 0) || any(reprojUV > 1))
	{
		historyWeight = 0.0f;
	}
	cPassData.Output.Store(threadID.xy, lerp(ao, prevAO, historyWeight));
}
