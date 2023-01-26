#include "Common.hlsli"

#define BLOCK_SIZE 8

#define TEXTURE_STORAGE float4
#define TEXTURE_INPUT_TYPE Texture2D<TEXTURE_STORAGE>
#define TEXTURE_OUTPUT_TYPE RWTexture2D<TEXTURE_STORAGE>

struct PassParameters
{
	uint2 TargetDimensions;
	float2 TargetDimensionsInv;
};

ConstantBuffer<PassParameters> cPassData : register(b0);

TEXTURE_OUTPUT_TYPE uOutput : register(u0);
TEXTURE_INPUT_TYPE tInput : register(t0);

[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void CSMain(uint3 threadId : SV_DispatchThreadID)
{
	if(all(threadId.xy < cPassData.TargetDimensions))
	{
		uOutput[threadId.xy] = tInput.SampleLevel(sLinearClamp, ((float2)threadId.xy + 0.5f) * cPassData.TargetDimensionsInv, 0);
	}
}
