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