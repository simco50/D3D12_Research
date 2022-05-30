#include "Common.hlsli"
#include "Noise.hlsli"

struct PassParameters
{
	uint Frequency;
	float ResolutionInv;
	uint Seed;
};

ConstantBuffer<PassParameters> cPass : register(b0);
RWTexture3D<float4> uNoiseOutput : register(u0);
RWTexture2D<float> uHeightGradient : register(u0);

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

float WorleyFBM(float3 uvw, float frequency)
{
	return
		saturate(InverseLerp(WorleyNoise(uvw, frequency, cPass.Seed), 0.15f, 0.85f)) * 0.625f +
		saturate(InverseLerp(WorleyNoise(uvw, frequency * 2, cPass.Seed), 0.15f, 0.85f)) * 0.25f +
		saturate(InverseLerp(WorleyNoise(uvw, frequency * 4, cPass.Seed), 0.15f, 0.85f)) * 0.125f;
}

[numthreads(8, 8, 8)]
void CloudShapeNoiseCS(uint3 threadId : SV_DispatchThreadID)
{
	float3 uvw = (threadId.xyz + 0.5f) * (float)cPass.ResolutionInv;

	float4 noiseResults = 0;
	noiseResults.y = WorleyFBM(uvw, cPass.Frequency);
	noiseResults.z = WorleyFBM(uvw, cPass.Frequency * 2);
	noiseResults.w = WorleyFBM(uvw, cPass.Frequency * 4);

    // Seven octave low frequency perlin FBM
	float perlin = PerlinFBM(uvw, 3, 7);
	noiseResults.x = Remap(perlin, 0.0f, 1.0f, noiseResults.y, 1.0f);

	uNoiseOutput[threadId.xyz] = noiseResults;
}

[numthreads(8, 8, 8)]
void CloudDetailNoiseCS(uint3 threadId : SV_DispatchThreadID)
{
	float3 uvw = (threadId.xyz + 0.5f) * (float)cPass.ResolutionInv;

	float4 noiseResults = 0;
	noiseResults.x = WorleyFBM(uvw, cPass.Frequency);
	noiseResults.y = WorleyFBM(uvw, cPass.Frequency * 2);
	noiseResults.z = WorleyFBM(uvw, cPass.Frequency * 4);
	noiseResults.w = WorleyFBM(uvw, cPass.Frequency * 8);

	uNoiseOutput[threadId.xyz] = noiseResults;
}

[numthreads(8, 8, 8)]
void CloudHeightDensityCS(uint3 threadId : SV_DispatchThreadID)
{
	float3 uvw = (threadId.xyz + 0.5f) * (float)cPass.ResolutionInv;
	float cloudType = uvw.x;
	float height = uvw.y;

	const float4 stratoGradient = float4(0.0f, 0.07f, 0.08f, 0.15f);
	const float4 stratoCumulusGradient = float4(0.0f, 0.2f, 0.42f, 0.6f);
	const float4 culumulusGradient = float4(0.0f, 0.08f, 0.75f, 0.98f);

	float a = 1.0f - saturate(cloudType * 2.0f);
	float b = 1.0f - abs(cloudType - 0.5f) * 2.0f;
	float c = saturate(cloudType - 0.5f) * 2.0f;
	float4 gradient = stratoGradient * a + stratoCumulusGradient * b + culumulusGradient * c;

	float v1 = saturate(InverseLerp(height, gradient.x, gradient.y));
	float v2 = saturate(InverseLerp(height, gradient.w, gradient.z));
	float v = v1 * v2;

	uHeightGradient[threadId.xy] = v;
}
