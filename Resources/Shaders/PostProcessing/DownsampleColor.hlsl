#include "Common.hlsli"

struct PassParams
{
	uint2 TargetDimensions;
	float2 TargetDimensionsInv;
	RWTexture2DH<float4> Output;
	Texture2DH<float4> Input;
};
DEFINE_CONSTANTS(PassParams, 0);

[numthreads(8, 8, 1)]
void CSMain(uint3 threadId : SV_DispatchThreadID)
{
	if(all(threadId.xy < cPassParams.TargetDimensions))
		cPassParams.Output.Store(threadId.xy, cPassParams.Input.SampleLevel(sLinearClamp, TexelToUV(threadId.xy, cPassParams.TargetDimensionsInv), 0));
}
