#pragma once

#include "Constants.hlsli"
//-----------------------------------------------------------------------------------------

// Quick And Easy GPU Random Numbers In D3D11 - Nathan Reed - 2013
// http://www.reedbeta.com/blog/quick-and-easy-gpu-random-numbers-in-d3d11/
// Hash Functions for GPU Rendering - Nathan Reed
// https://www.reedbeta.com/blog/hash-functions-for-gpu-rendering/

uint SeedThread(uint seed)
{
#if 0
    //Wang hash to initialize the seed
    seed = (seed ^ 61) ^ (seed >> 16);
    seed *= 9;
    seed = seed ^ (seed >> 4);
    seed *= 0x27d4eb2d;
    seed = seed ^ (seed >> 15);
    return seed;
#else
  	uint state = seed * 747796405u + 2891336453u;
  	uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
   	return (word >> 22u) ^ word;
#endif
}

uint SeedThread(uint2 pixel, uint2 resolution, uint frameIndex)
{
    uint rngState = dot(pixel, uint2(1, resolution.x)) ^ SeedThread(frameIndex);
    return SeedThread(rngState);
}

uint XORShift(inout uint rng_state)
{
    // Xorshift algorithm from George Marsaglia's paper.
    rng_state ^= (rng_state << 13);
    rng_state ^= (rng_state >> 17);
    rng_state ^= (rng_state << 5);
    return rng_state;
}

float Random01(inout uint rng_state)
{
    return asfloat(0x3f800000 | XORShift(rng_state) >> 9) - 1.0;
}

uint Random(inout uint rng_state, uint minimum, uint maximum)
{
    return minimum + uint(float(maximum - minimum + 1) * Random01(rng_state));
}

//-----------------------------------------------------------------------------------------

//Van Der Corpus Sequence
//source: http://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html
float VanDerCorpusSequence(uint bits)
{
	bits = (bits << 16u) | (bits >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
	return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

//Hammersley Points on the Hemisphere - Holger Dammertz - 2012
//source: http://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html
float2 HammersleyPoints(uint i, uint N)
{
    return float2(float(i) / float(N), VanDerCorpusSequence(i));
}

//hemisphereSample_uniform. Uniform distribution on the sphere
float3 HemisphereSampleUniform(float u, float v)
{
	float phi = v * 2.0 * PI;
	float cosTheta = 1.0 - u;
	float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
	return float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

// Samples a direction within a hemisphere oriented along +Z axis with a cosine-weighted distribution
// Source: "Sampling Transformations Zoo" in Ray Tracing Gems by Shirley et al.
float3 HemisphereSampleCosineWeight(float2 u, out float pdf)
{
	float a = sqrt(u.x);
	float b = 2.0f * PI * u.y;

	float3 result = float3(
		a * cos(b),
		a * sin(b),
		sqrt(1.0f - u.x));

	pdf = result.z * INV_PI;

	return result;
}

//-----------------------------------------------------------------------------------------
