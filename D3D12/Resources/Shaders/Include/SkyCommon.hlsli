#ifndef __SKY_COMMON_INCLUDE__
#define __SKY_COMMON_INCLUDE__

float AngleBetween(float3 dir0, float3 dir1)
{
	return acos(dot(dir0, dir1));
}

//A Practical Analytic Model for Daylight
//A. J. Preetham, Peter Shirley, Brian Smits
//https://dl.acm.org/doi/pdf/10.1145/311535.311545

float3 CIESky(float3 dir, float3 sunDir, bool withSun = true)
{
	float3 skyDir = float3(dir.x, saturate(dir.y), dir.z);
	float gamma = AngleBetween(skyDir, sunDir);
	float S = AngleBetween(sunDir, float3(0, 1, 0));
	float theta = AngleBetween(skyDir, float3(0, 1, 0));

	float cosTheta = cos(theta);
	float cosS = cos(S);
	float cosGamma = cos(gamma);

	float numerator = (0.91f + 10 * exp(-3 * gamma) + 0.45 * cosGamma * cosGamma) * (1 - exp(-0.32f / cosTheta));
	float denominator = (0.91f + 10 * exp(-3 * S) + 0.45 * cosS * cosS) * (1 - exp(-0.32f));

	float luminance = numerator / max(denominator, 0.0001f);

	// Clear Sky model only calculates luminance, so we'll pick a strong blue color for the sky
	const float3 SkyColor = float3(0.2f, 0.5f, 1.0f) * 1;
	const float3 SunColor = float3(1.0f, 0.8f, 0.3f) * 1500;
	const float SunWidth = 0.04f;

	float3 color = SkyColor;

	if(withSun)
	{
  		// Draw a circle for the sun
		float sunGamma = AngleBetween(dir, sunDir);
		color = lerp(SunColor, SkyColor, saturate(abs(sunGamma) / SunWidth));
	}

	return max(color * luminance, 0);
}

#endif