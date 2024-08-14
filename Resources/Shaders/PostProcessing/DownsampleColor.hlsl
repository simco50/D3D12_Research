#include "Common.hlsli"

struct PassParameters
{
	uint2 TargetDimensions;
	float2 TargetDimensionsInv;
};

ConstantBuffer<PassParameters> cPassData : register(b0);

RWTexture2D<float4> uOutput : register(u0);
Texture2D<float4> tInput : register(t0);

[numthreads(8, 8, 1)]
void CSMain(uint3 threadId : SV_DispatchThreadID)
{
	if(all(threadId.xy < cPassData.TargetDimensions))
		uOutput[threadId.xy] = tInput.SampleLevel(sLinearClamp, TexelToUV(threadId.xy, cPassData.TargetDimensionsInv), 0);
}
