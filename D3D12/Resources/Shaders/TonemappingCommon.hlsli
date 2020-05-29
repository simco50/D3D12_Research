#include "Common.hlsli"

#define NUM_HISTOGRAM_BINS 64

#define TONEMAP_LUMINANCE 0

#if TONEMAP_LUMINANCE
#define TONEMAP_TYPE float
#else
#define TONEMAP_TYPE float3
#endif

TONEMAP_TYPE Reinhard(TONEMAP_TYPE x)
{
	return x / (1.0 + x);
}

TONEMAP_TYPE ReinhardExtended(TONEMAP_TYPE x, float MaxWhite)
{
	return (x * (1.0 + x / Square(MaxWhite)) ) / (1.0 + x);
}

TONEMAP_TYPE ACES_Fast(TONEMAP_TYPE x) 
{
    // Narkowicz 2015, "ACES Filmic Tone Mapping Curve"
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return (x * (a * x + b)) / (x * (c * x + d) + e);
}

TONEMAP_TYPE Unreal3(TONEMAP_TYPE x) 
{
    // Unreal 3, Documentation: "Color Grading"
    // Adapted to be close to Tonemap_ACES, with similar range
    // Gamma 2.2 correction is baked in, don't use with sRGB conversion!
    return x / (x + 0.155) * 1.019;
}

TONEMAP_TYPE Uncharted2(TONEMAP_TYPE x)
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

//https://google.github.io/filament/Filament.md.html#imagingpipeline/physicallybasedcamera/exposurevalue
float EV100FromLuminance(float luminance)
{
	const float K = 12.5f; //Reflected-light meter constant
	const float ISO = 100.0f;
	return log2(luminance * (ISO / K));
}

float Exposure(float ev100)
{
	return 1.0f / (pow(2.0f, ev100) * 1.2f);
}

float GetLuminance(float3 color)
{
    return dot(color, float3(0.2127f, 0.7152f, 0.0722f));
}