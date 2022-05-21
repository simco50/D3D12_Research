#include "Common.hlsli"
#include "Random.hlsli"

struct PassParameters
{
	uint4 PointsPerRow;
	uint Resolution;
	uint Seed;
};

ConstantBuffer<PassParameters> cPass : register(b0);
RWTexture3D<float4> uOutputTexture : register(u0);

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
				float3 p = Hash(pointIndex) + offset;
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

	float4 noiseResult = 0;
	for(uint channel = 0; channel < 4; ++channel)
	{
		uint pointsPerRow = cPass.PointsPerRow[channel];
		noiseResult[channel] = WorleyFBM(uvw, pointsPerRow);
	}

	uOutputTexture[threadId.xyz] = noiseResult;
}