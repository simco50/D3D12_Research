#pragma once

/*
Helper functions to transform between different color spaces.
Usually, to jump between spaces, you transform to CIE XYZ and from there you can transform into anything
Super useful page for different transformations: http://www.brucelindbloom.com/index.html?Eqn_xyY_to_XYZ.html
*/


float3 sRGB_to_XYZ(float3 rgb)
{
	//http://www.brucelindbloom.com/index.html?Eqn_RGB_to_XYZ.html
	float3 xyz;
	xyz.x = dot(float3(0.4124564, 0.3575761, 0.1804375), rgb);
	xyz.y = dot(float3(0.2126729, 0.7151522, 0.0721750), rgb);
	xyz.z = dot(float3(0.0193339, 0.1191920, 0.9503041), rgb);
	return xyz;
}

float3 XYZ_to_sRGB(float3 xyz)
{
	//http://www.brucelindbloom.com/index.html?Eqn_RGB_to_XYZ.html
	float3 rgb;
	rgb.x = dot(float3( 3.2404542, -1.5371385, -0.4985314), xyz);
	rgb.y = dot(float3(-0.9692660,  1.8760108,  0.0415560), xyz);
	rgb.z = dot(float3( 0.0556434, -0.2040259,  1.0572252), xyz);
	return rgb;
}

float3 XYZ_to_xyY(float3 xyz)
{
	//http://www.brucelindbloom.com/index.html?Eqn_XYZ_to_xyY.html
	float inv = 1.0 / dot(xyz, float3(1.0, 1.0, 1.0));
	return float3(xyz.x*inv, xyz.y*inv, xyz.y);
}

float3 xyY_to_XYZ(float3 xyY)
{
	//http://www.brucelindbloom.com/index.html?Eqn_xyY_to_XYZ.html
	float3 xyz;
	xyz.x = xyY.x * xyY.z / xyY.y;
	xyz.y = xyY.z;
	xyz.z = xyY.z * (1.0 - xyY.x - xyY.y) / xyY.y;
	return xyz;
}

float3 sRGB_to_xyY(float3 rgb)
{
	return XYZ_to_xyY(sRGB_to_XYZ(rgb));
}

float3 xyY_to_sRGB(float3 xyY)
{
	return XYZ_to_sRGB(xyY_to_XYZ(xyY));
}

// https://software.intel.com/en-us/node/503873
float3 RGB_to_YCoCg(float3 c)
{
	// Y = R/4 + G/2 + B/4
	// Co = R/2 - B/2
	// Cg = -R/4 + G/2 - B/4
	return float3(
			c.x/4.0 + c.y/2.0 + c.z/4.0,
			c.x/2.0 - c.z/2.0,
		-c.x/4.0 + c.y/2.0 - c.z/4.0
	);
}

// https://software.intel.com/en-us/node/503873
float3 YCoCg_to_RGB(float3 c)
{
	// R = Y + Co - Cg
	// G = Y + Cg
	// B = Y - Co - Cg
	return float3(
		c.x + c.y - c.z,
		c.x + c.z,
		c.x - c.y - c.z
	);
}
