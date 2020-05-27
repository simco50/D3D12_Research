#include "Common.hlsli"
#include "TonemappingOperators.hlsli"

#define TONEMAP_REINHARD 0
#define TONEMAP_REINHARD_EXTENDED 1
#define TONEMAP_ACES_FAST 2
#define TONEMAP_UNREAL3 3
#define TONEMAP_UNCHARTED2 4

#define RootSig "CBV(b0, visibility=SHADER_VISIBILITY_ALL), " \
				"DescriptorTable(SRV(t0, numDescriptors = 2), visibility=SHADER_VISIBILITY_PIXEL), " \
				"StaticSampler(s0, filter=FILTER_MIN_MAG_MIP_LINEAR, visibility = SHADER_VISIBILITY_PIXEL), " \

struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD;
};

cbuffer Parameters : register(b0)
{
	float cWhitePoint;
	uint cTonemapper;
}

float3 ConvertRGB2XYZ(float3 rgb)
{
	// Reference(s):
	// - RGB/XYZ Matrices
	//   https://web.archive.org/web/20191027010220/http://www.brucelindbloom.com/index.html?Eqn_RGB_XYZ_Matrix.html
	float3 xyz;
	xyz.x = dot(float3(0.4124564, 0.3575761, 0.1804375), rgb);
	xyz.y = dot(float3(0.2126729, 0.7151522, 0.0721750), rgb);
	xyz.z = dot(float3(0.0193339, 0.1191920, 0.9503041), rgb);
	return xyz;
}

float3 ConvertXYZ2RGB(float3 xyz)
{
	float3 rgb;
	rgb.x = dot(float3( 3.2404542, -1.5371385, -0.4985314), xyz);
	rgb.y = dot(float3(-0.9692660,  1.8760108,  0.0415560), xyz);
	rgb.z = dot(float3( 0.0556434, -0.2040259,  1.0572252), xyz);
	return rgb;
}

float3 ConvertXYZ2Yxy(float3 xyz)
{
	// Reference(s):
	// - XYZ to xyY
	//   https://web.archive.org/web/20191027010144/http://www.brucelindbloom.com/index.html?Eqn_XYZ_to_xyY.html
	float inv = 1.0/dot(xyz, float3(1.0, 1.0, 1.0) );
	return float3(xyz.y, xyz.x*inv, xyz.y*inv);
}

float3 ConvertYxy2XYZ(float3 Yxy)
{
	// Reference(s):
	// - xyY to XYZ
	//   https://web.archive.org/web/20191027010036/http://www.brucelindbloom.com/index.html?Eqn_xyY_to_XYZ.html
	float3 xyz;
	xyz.x = Yxy.x*Yxy.y/Yxy.z;
	xyz.y = Yxy.x;
	xyz.z = Yxy.x*(1.0 - Yxy.y - Yxy.z)/Yxy.z;
	return xyz;
}

float3 ConvertRGB2Yxy(float3 rgb)
{
	return ConvertXYZ2Yxy(ConvertRGB2XYZ(rgb));
}

float3 ConvertYxy2RGB(float3 Yxy)
{
	return ConvertXYZ2RGB(ConvertYxy2XYZ(Yxy));
}

Texture2D tColor : register(t0);
SamplerState sColorSampler : register(s0);
StructuredBuffer<float> tAverageLuminance : register(t1);

[RootSignature(RootSig)]
PSInput VSMain(uint index : SV_VERTEXID)
{
	PSInput output;
	output.position.x = (float)(index / 2) * 4.0f - 1.0f;
	output.position.y = (float)(index % 2) * 4.0f - 1.0f;
	output.position.z = 0.0f;
	output.position.w = 1.0f;

	output.texCoord.x = (float)(index / 2) * 2.0f;
	output.texCoord.y = 1.0f - (float)(index % 2) * 2.0f;

	return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	float3 rgb = tColor.Sample(sColorSampler, input.texCoord).rgb;
    float avgLum = tAverageLuminance[0];

	/* Long version - https://google.github.io/filament/Filament.md.html#imagingpipeline/physicallybasedcamera/exposurevalue
	const float ISO = 100.0f;
	const float K = 12.5f;
	const float q = 0.65f;
	float EV = log2((avgLum * ISO) / K);
	float EV100 = EV - log2(ISO / 100);
	float lMax = pow(2, EV100) * 1.2f;
	float newLuminance = Yxy.x / lMax;
	*/

#if TONEMAP_LUMINANCE
    float3 Yxy = ConvertRGB2Yxy(rgb);
    float value = Yxy.x / (9.6 * avgLum + 0.0001f);
#else
	float3 value = rgb / (9.6 * avgLum + 0.0001f);
#endif

	switch(cTonemapper)
	{
	case TONEMAP_REINHARD:
    	value = Reinhard(value);
		break;
	case TONEMAP_REINHARD_EXTENDED:
    	value = ReinhardExtended(value, cWhitePoint);
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
	Yxy.x = value;
	rgb = ConvertYxy2RGB(Yxy);
#else
	rgb = value;
#endif

	if(cTonemapper != TONEMAP_UNREAL3)
	{
		rgb = LinearToSrgbFast(rgb);
	}
	return float4(rgb, 1);
}