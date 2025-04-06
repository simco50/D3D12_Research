#include "Common.hlsli"
#include "Color.hlsli"

#define TONEMAP_REINHARD 0
#define TONEMAP_REINHARD_EXTENDED 1
#define TONEMAP_ACES_FAST 2
#define TONEMAP_UNREAL3 3
#define TONEMAP_UNCHARTED2 4

#define BLOCK_SIZE 16

struct PassParams
{
	float WhitePoint;
	uint Tonemapper;
	float BloomIntensity;
	float BloomBlendFactor;
	float3 LensDirtTint;

	RWTexture2DH<float4> OutColor;
	Texture2DH<float4> Color;
	StructuredBufferH<float> AverageLuminance;
	Texture2DH<float4> Bloom;
	Texture2DH<float4> LensDirt;
};
DEFINE_CONSTANTS(PassParams, 0);

template<typename T>
T Reinhard(T x)
{
	return x / (1.0 + x);
}

template<typename T>
T InverseReinhard(T x)
{
	return x / (1.0 - x);
}

template<typename T>
T ReinhardExtended(T x, float MaxWhite)
{
	return (x * (1.0 + x / Square(MaxWhite)) ) / (1.0 + x);
}

template<typename T>
T ACES_Fast(T x)
{
	// Narkowicz 2015, "ACES Filmic Tone Mapping Curve"
	const float a = 2.51;
	const float b = 0.03;
	const float c = 2.43;
	const float d = 0.59;
	const float e = 0.14;
	return (x * (a * x + b)) / (x * (c * x + d) + e);
}

template<typename T>
T Unreal3(T x)
{
	// Unreal 3, Documentation: "Color Grading"
	// Adapted to be close to Tonemap_ACES, with similar range
	// Gamma 2.2 correction is baked in, don't use with sRGB conversion!
	return x / (x + 0.155) * 1.019;
}

template<typename T>
T Uncharted2(T x)
{
	const float A = 0.15;
	const float B = 0.50;
	const float C = 0.10;
	const float D = 0.20;
	const float E = 0.02;
	const float F = 0.30;
	const float W = 11.2;
	return ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F))-E/F;
}

[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	if(any(dispatchThreadId.xy >= cView.ViewportDimensions))
		return;

	float2 uv = (0.5f + dispatchThreadId.xy) * cView.ViewportDimensionsInv;

	float3 rgb = cPassParams.Color.Load(uint3(dispatchThreadId.xy, 0)).rgb;

	if(cPassParams.BloomIntensity > 0)
	{
		float3 lensDirt = cPassParams.LensDirt.SampleLevel(sLinearClamp, float2(uv.x, 1.0f - uv.y), 0).rgb * cPassParams.LensDirtTint;
		float3 bloom = cPassParams.Bloom.SampleLevel(sLinearClamp, uv, 0).rgb * cPassParams.BloomIntensity;
		rgb = lerp(rgb, bloom + bloom * lensDirt, cPassParams.BloomBlendFactor);
	}

#if TONEMAP_LUMINANCE
	float3 xyY = sRGB_to_xyY(rgb);
	float value = xyY.z;
#else
	float3 value = rgb;
#endif

	float exposure = cPassParams.AverageLuminance[2];
	value = value * (exposure + 1);

	switch(cPassParams.Tonemapper)
	{
	case TONEMAP_REINHARD:
		value = Reinhard(value);
		break;
	case TONEMAP_REINHARD_EXTENDED:
		value = ReinhardExtended(value, cPassParams.WhitePoint);
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

	if(cPassParams.Tonemapper != TONEMAP_UNREAL3)
	{
		rgb = LinearToSRGB(rgb);
	}
	cPassParams.OutColor.Store(dispatchThreadId.xy, float4(rgb, 1));
}
