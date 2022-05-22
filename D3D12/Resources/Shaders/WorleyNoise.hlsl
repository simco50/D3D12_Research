#include "Common.hlsli"
#include "Random.hlsli"

struct PassParameters
{
	uint Frequency;
	uint Resolution;
	uint Seed;
};

ConstantBuffer<PassParameters> cPass : register(b0);
RWTexture3D<float4> uOutputTexture : register(u0);

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

// From https://iquilezles.org/articles/gradientnoise/
float GradientNoise(float3 x, float freq)
{
    // grid
    uint3 p = floor(x);
    float3 w = frac(x);
    
    // quintic interpolant
    float3 u = w * w * w * (w * (w * 6. - 15.) + 10.);
    
    // gradients
    float3 ga = hash33((p + float3(0., 0., 0.)) % freq);
    float3 gb = hash33((p + float3(1., 0., 0.)) % freq);
    float3 gc = hash33((p + float3(0., 1., 0.)) % freq);
    float3 gd = hash33((p + float3(1., 1., 0.)) % freq);
    float3 ge = hash33((p + float3(0., 0., 1.)) % freq);
    float3 gf = hash33((p + float3(1., 0., 1.)) % freq);
    float3 gg = hash33((p + float3(0., 1., 1.)) % freq);
    float3 gh = hash33((p + float3(1., 1., 1.)) % freq);
    
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

// Fbm for Perlin noise based on iq's blog
float PerlinFBM(float3 p, uint freq, int octaves)
{
    const float H = 0.85f;
    float G = exp2(-H);
    float amp = 1.0f;
    float noise = 0.0f;
    for (int i = 0; i < octaves; ++i)
    {
        noise += amp * GradientNoise(p * freq, freq);
        freq *= 2.0f;
        amp *= G;
    }
    
    return noise;
}

float3 Hash(uint index)
{
	uint seed = SeedThread(cPass.Seed + index);
	return float3(Random01(seed), Random01(seed), Random01(seed));
}

float WorleyNoise(float3 uvw, uint pointsPerRow)
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
				uint pointIndex = Flatten3D(neighbourCellWrappedId, pointsPerRow);
				float3 p = Hash(pointsPerRow * pointIndex) + offset;
				minDistSq = min(minDistSq, dot(frc - p, frc - p));
			}
		}
	}
	return 1.0f - sqrt(minDistSq);
}

float WorleyFBM(float3 uvw, float frequency)
{
	return 
		WorleyNoise(uvw, frequency) * 0.625f +
		WorleyNoise(uvw, frequency * 2) * 0.25f +
		WorleyNoise(uvw, frequency * 4) * 0.125f;
}

[numthreads(8, 8, 8)]
void WorleyNoiseCS(uint3 threadId : SV_DISPATCHTHREADID)
{
	float3 uvw = threadId.xyz / (float)cPass.Resolution;

	float4 noiseResults = 0;
	noiseResults.y = WorleyFBM(uvw, cPass.Frequency);
	noiseResults.z = WorleyFBM(uvw, cPass.Frequency * 2);
	noiseResults.w = WorleyFBM(uvw, cPass.Frequency * 4);

    // Seven octave low frequency perlin FBM
	float perlin = PerlinFBM(uvw, 3, 7);
	noiseResults.x = Remap(perlin, 0.0f, 1.0f, noiseResults.y, 1.0f);
	
	uOutputTexture[threadId.xyz] = noiseResults;
}