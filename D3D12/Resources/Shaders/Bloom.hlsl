#include "Common.hlsli"

Texture2D<float4> tSource : register(t0);
Texture2D<float4> tPreviousSource : register(t1);
RWTexture2D<float4> uTarget : register(u0);

struct DownsampleParams
{
	float2 TargetDimensionsInv;
	uint SourceMip;
};
ConstantBuffer<DownsampleParams> cDownsampleParams : register(b0);

[numthreads(8, 8, 1)]
void DownsampleCS(uint3 threadId : SV_DispatchThreadID)
{
	float2 UV = (threadId.xy + 0.5f) * cDownsampleParams.TargetDimensionsInv;

	const int2 sampleOffsets[] = {
		int2(-1.0f,  1.0f), int2(1.0f,  1.0f),
		int2(-1.0f, -1.0f), int2(1.0f, -1.0f),

		int2(-2.0f,  2.0f), int2(0.0f,  2.0f), int2(2.0f,  2.0f),
		int2(-2.0f,  0.0f), int2(0.0f,  0.0f), int2(2.0f,  0.0f),
		int2(-2.0f, -2.0f), int2(0.0f, -2.0f), int2(2.0f, -2.0f),
	};

	const float sampleWeights[] = {
		0.125f, 0.125f,
		0.125f, 0.125f,

		0.0555555f, 0.0555555f, 0.0555555f,
		0.0555555f, 0.0555555f, 0.0555555f,
		0.0555555f, 0.0555555f, 0.0555555f,
	};
	float3 outColor = 0;
	[unroll]
	for(uint i = 0; i < 13; ++i)
	{
		outColor += sampleWeights[i] * tSource.SampleLevel(sLinearClamp, UV, cDownsampleParams.SourceMip, sampleOffsets[i]).xyz;
	}
	uTarget[threadId.xy] = float4(outColor, 1);
}

struct UpsampleParams
{
	float2 TargetDimensionsInv;
	uint SourceCurrentMip;
	uint SourcePreviousMip;
	float Radius;
};
ConstantBuffer<UpsampleParams> cUpsampleParams : register(b0);

[numthreads(8, 8, 1)]
void UpsampleCS(uint3 threadId : SV_DispatchThreadID)
{
	float2 UV = (threadId.xy + 0.5f) * cUpsampleParams.TargetDimensionsInv;
	float3 currentColor = tSource.SampleLevel(sLinearClamp, UV, cUpsampleParams.SourceCurrentMip).xyz;

	const int2 sampleOffsets[] = {
        int2( -1.0f,  1.0f ), int2(  0.0f,  1.0f ), int2(  1.0f,  1.0f ),
        int2( -1.0f,  0.0f ), int2(  0.0f,  0.0f ), int2(  1.0f,  0.0f ),
        int2( -1.0f, -1.0f ), int2(  0.0f, -1.0f ), int2(  1.0f, -1.0f )
    };

    const float sampleWeights[] = {
        0.0625f, 0.125f, 0.0625f,
        0.125f,  0.25f,  0.125f,
        0.0625f, 0.125f, 0.0625f
    };
	float3 outColor = 0;
	[unroll]
	for(uint i = 0; i < 9; ++i)
	{
		outColor += sampleWeights[i] * tPreviousSource.SampleLevel(sLinearClamp, UV, cUpsampleParams.SourcePreviousMip, sampleOffsets[i]).xyz;
	}

	uTarget[threadId.xy] = float4(lerp(currentColor, outColor, cUpsampleParams.Radius) , 1);
}
