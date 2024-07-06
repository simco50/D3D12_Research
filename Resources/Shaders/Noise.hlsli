#include "Common.hlsli"
#include "Random.hlsli"

// David Hoskins Hash
#define UI0 1597334673U
#define UI1 3812015801U
#define UI2 uint2(UI0, UI1)
#define UI3 uint3(UI0, UI1, 2798796415U)
#define UIF (1.0 / float(0xffffffffU))

float3 hash33(float3 p)
{
	uint3 q = uint3(p) * UI3;
	q = (q.x ^ q.y ^ q.z)*UI3;
	return -1. + 2. * float3(q) * UIF;
}

float2 hash33(float2 p)
{
	uint2 q = uint2(p) * UI2;
	q = (q.x ^ q.y)*UI2;
	return -1. + 2. * float2(q) * UIF;
}

// From https://iquilezles.org/articles/gradientnoise/ converted to HLSL

// returns 2D value noise (in .x)  and its derivatives (in .yz)
float3 GradientNoise(in float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);

    float2 u = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
    float2 du = 30.0f* f * f * (f * (f - 2.0f) + 1.0f);

    float2 ga = hash33(i + float2(0.0f, 0.0f));
    float2 gb = hash33(i + float2(1.0f, 0.0f));
    float2 gc = hash33(i + float2(0.0f, 1.0f));
    float2 gd = hash33(i + float2(1.0f, 1.0f));

    float va = dot(ga, f - float2(0.0f, 0.0f));
    float vb = dot(gb, f - float2(1.0f, 0.0f));
    float vc = dot(gc, f - float2(0.0f, 1.0f));
    float vd = dot(gd, f - float2(1.0f, 1.0f));

    return float3(
		va + u.x * (vb - va) + u.y * (vc - va) + u.x * u.y * (va - vb - vc + vd),	// value
        ga + u.x * (gb - ga) + u.y * (gc - ga) + u.x * u.y * (ga - gb - gc + gd) +  // derivatives
    	du * (u.yx * (va - vb - vc + vd) + float2(vb, vc) - va)
	);
}

float3 WrapNoiseValue(float3 value, float frequency)
{
    return frac(value / frequency) * frequency;
}

// 3D Wrapped version
float GradientNoise(float3 x, float freq)
{
    // grid
    uint3 p = floor(x);
    float3 w = frac(x);

    // quintic interpolant
    float3 u = w * w * w * (w * (w * 6. - 15.) + 10.);

    // gradients
    float3 ga = hash33(WrapNoiseValue(p + float3(0., 0., 0.), freq));
    float3 gb = hash33(WrapNoiseValue(p + float3(1., 0., 0.), freq));
    float3 gc = hash33(WrapNoiseValue(p + float3(0., 1., 0.), freq));
    float3 gd = hash33(WrapNoiseValue(p + float3(1., 1., 0.), freq));
    float3 ge = hash33(WrapNoiseValue(p + float3(0., 0., 1.), freq));
    float3 gf = hash33(WrapNoiseValue(p + float3(1., 0., 1.), freq));
    float3 gg = hash33(WrapNoiseValue(p + float3(0., 1., 1.), freq));
    float3 gh = hash33(WrapNoiseValue(p + float3(1., 1., 1.), freq));

    // projections
    float va = dot(ga, w - float3(0., 0., 0.));
    float vb = dot(gb, w - float3(1., 0., 0.));
    float vc = dot(gc, w - float3(0., 1., 0.));
    float vd = dot(gd, w - float3(1., 1., 0.));
    float ve = dot(ge, w - float3(0., 0., 1.));
    float vf = dot(gf, w - float3(1., 0., 1.));
    float vg = dot(gg, w - float3(0., 1., 1.));
    float vh = dot(gh, w - float3(1., 1., 1.));

    // interpolation
    return va +
           u.x * (vb - va) +
           u.y * (vc - va) +
           u.z * (ve - va) +
           u.x * u.y * (va - vb - vc + vd) +
           u.y * u.z * (va - vc - ve + vg) +
           u.z * u.x * (va - vb - ve + vf) +
           u.x * u.y * u.z * (-va + vb + vc - vd + ve - vf - vg + vh);
}

float WorleyNoise(float3 uvw, uint pointsPerRow, uint seed)
{
	uvw *= pointsPerRow;
	float3 frc = frac(uvw);
	uint3 i = floor(uvw);

	float minDistSq = 1;
	for (int x = -1; x <= 1; ++x)
	{
		for (int y = -1; y <= 1; ++y)
		{
			for (int z = -1; z <= 1; ++z)
			{
                int3 offset = int3(x, y, z);
				int3 neighbourCellWrappedId = int3(i + offset + pointsPerRow) % pointsPerRow;
				uint pSeed = SeedThread(seed + Flatten3D(neighbourCellWrappedId, pointsPerRow));
				float3 p = float3(Random01(pSeed), Random01(pSeed), Random01(pSeed)) + offset;
				minDistSq = min(minDistSq, dot(frc - p, frc - p));
			}
		}
	}
	return 1.0f - sqrt(minDistSq);
}

//From "NEXT GENERATION POST PROCESSING IN CALL OF DUTY: ADVANCED WARFARE"
//http://advances.realtimerendering.com/s2014/index.html
float InterleavedGradientNoise(float2 uv)
{
	const float3 magic = float3(0.06711056f, 0.00583715f, 52.9829189f);
	return frac(magic.z * frac(dot(uv, magic.xy)));
}
float InterleavedGradientNoise(float2 uv, float offset)
{
	uv += offset * (float2(47, 17) * 0.695f);
	const float3 magic = float3( 0.06711056f, 0.00583715f, 52.9829189f );
	return frac(magic.z * frac(dot(uv, magic.xy)));
}
