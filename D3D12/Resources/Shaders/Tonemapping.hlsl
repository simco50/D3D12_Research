#include "Common.hlsli"
#include "TonemappingCommon.hlsli"
#include "Color.hlsli"

#define TONEMAP_REINHARD 0
#define TONEMAP_REINHARD_EXTENDED 1
#define TONEMAP_ACES_FAST 2
#define TONEMAP_UNREAL3 3
#define TONEMAP_UNCHARTED2 4

#define BLOCK_SIZE 16

struct PassParameters
{
	float WhitePoint;
	uint Tonemapper;
	float BloomIntensity;
	float BloomBlendFactor;
	float3 LensDirtTint;
};

ConstantBuffer<PassParameters> cPassData : register(b0);
RWTexture2D<float4> uOutColor : register(u0);
Texture2D tColor : register(t0);
StructuredBuffer<float> tAverageLuminance : register(t1);
Texture2D tBloom : register(t2);
Texture2D tLensDirt : register(t3);

[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	if(any(dispatchThreadId.xy >= cView.TargetDimensions))
		return;

	float2 uv = (0.5f + dispatchThreadId.xy) * cView.TargetDimensionsInv;

	float3 rgb = tColor.Load(uint3(dispatchThreadId.xy, 0)).rgb;

	if(cPassData.BloomIntensity >= 0)
	{
		float3 lensDirt = tLensDirt.SampleLevel(sLinearClamp, float2(uv.x, 1.0f - uv.y), 0).rgb * cPassData.LensDirtTint;
		float3 bloom = tBloom.SampleLevel(sLinearClamp, uv, 0).rgb * cPassData.BloomIntensity;
		rgb = lerp(rgb, bloom + bloom * lensDirt, cPassData.BloomBlendFactor);
	}

#if TONEMAP_LUMINANCE
	float3 xyY = sRGB_to_xyY(rgb);
	float value = xyY.z;
#else
	float3 value = rgb;
#endif

	float exposure = tAverageLuminance[2];
	value = value * (exposure + 1);

	switch(cPassData.Tonemapper)
	{
	case TONEMAP_REINHARD:
		value = Reinhard(value);
		break;
	case TONEMAP_REINHARD_EXTENDED:
		value = ReinhardExtended(value, cPassData.WhitePoint);
		break;
	case TONEMAP_ACES_FAST:
		value = ACES_Fast(value);
		break;
	case TONEMAP_UNREAL3:
		value = Unreal3(value);
		break;
	case TONEMAP_UNCHARTED2:
		value = Uncharted2(value);
		break;
	}

#if TONEMAP_LUMINANCE
	xyY.z = value;
	rgb = xyY_to_sRGB(xyY);
#else
	rgb = value;
#endif

	if(cPassData.Tonemapper != TONEMAP_UNREAL3)
	{
		rgb = LinearToSrgbFast(rgb);
	}
	uOutColor[dispatchThreadId.xy] = float4(rgb, 1);
}
