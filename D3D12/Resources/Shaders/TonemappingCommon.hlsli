#ifndef __INCLUDE_TONEMAPPING_COMMON__
#define __INCLUDE_TONEMAPPING_COMMON__

#include "Common.hlsli"

#define NUM_HISTOGRAM_BINS 256

#ifndef TONEMAP_LUMINANCE 
#define TONEMAP_LUMINANCE 0
#endif

#if TONEMAP_LUMINANCE
#define TONEMAP_TYPE float
#else
#define TONEMAP_TYPE float3
#endif

TONEMAP_TYPE Reinhard(TONEMAP_TYPE x)
{
	return x / (1.0 + x);
}

TONEMAP_TYPE InverseReinhard(TONEMAP_TYPE x)
{
	return x / (1.0 - x);
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

float EV100FromLuminance(float luminance)
{
	//https://google.github.io/filament/Filament.md.html#imagingpipeline/physicallybasedcamera/exposurevalue
	const float K = 12.5f; //Reflected-light meter constant
	const float ISO = 100.0f;
	return log2(luminance * (ISO / K));
}

float Exposure(float ev100)
{
	//https://google.github.io/filament/Filament.md.html#imagingpipeline/physicallybasedcamera/exposurevalue
	return 1.0f / (pow(2.0f, ev100) * 1.2f);
}

float GetLuminance(float3 color)
{
    return dot(color, float3(0.2126729, 0.7151522, 0.0721750));
}

#endif