#include "CommonBindings.hlsli"

struct SeparateData
{
	float Threshold;
	float BrightnessClamp;
};

struct MipChainData
{
	uint SourceMip;
	float2 TargetDimensionsInv;
};

ConstantBuffer<SeparateData> cSeparateData : register(b0);
ConstantBuffer<MipChainData> cMipData : register(b0);

Texture2D<float4> tSource : register(t0);
StructuredBuffer<float> tAverageLuminance : register(t1);
RWTexture2D<float4> uTarget : register(u0);

[numthreads(8, 8, 1)]
void SeparateBloomCS(uint3 threadId : SV_DispatchThreadID)
{
	float2 UV = (0.5f + threadId.xy) * cView.ViewportDimensionsInv;
	float4 color = 0;
	color += tSource.SampleLevel(sLinearClamp, UV, 0, int2(-1, -1));
	color += tSource.SampleLevel(sLinearClamp, UV, 0, int2(1, -1));
	color += tSource.SampleLevel(sLinearClamp, UV, 0, int2(-1, 1));
	color += tSource.SampleLevel(sLinearClamp, UV, 0, int2(1, 1));
	color *= 0.25f;

	float exposure = tAverageLuminance[2];
	//color *= exposure;

	color = min(color, cSeparateData.BrightnessClamp);
	color = max(color - 0.5*cSeparateData.Threshold, 0);

	uTarget[threadId.xy] = float4(color.rgb, 1);
}

[numthreads(8, 8, 1)]
void BloomMipChainCS(uint3 threadId : SV_DispatchThreadID)
{
	float2 UV = (0.5f + threadId.xy) * cMipData.TargetDimensionsInv;

	float4 source = tSource.SampleLevel(sLinearClamp, UV, cMipData.SourceMip);
	uTarget[threadId.xy] = source;
}
