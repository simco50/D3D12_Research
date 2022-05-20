#include "Common.hlsli"
#include "Random.hlsli"

struct PassParameters
{
	uint4 PointsPerRow[4];
	uint4 InvertNoise;
	float4 Persistence;
	uint Resolution;
	uint Seed;
};

ConstantBuffer<PassParameters> cPass : register(b0);
RWTexture3D<float4> uOutputTexture : register(u0);

float3 GetPoint(uint index)
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
	for (int offsetZ = -1; offsetZ <= 1; ++offsetZ)
	{
		for (int offsetY = -1; offsetY <= 1; ++offsetY)
		{
			for (int offsetX = -1; offsetX <= 1; ++offsetX)
			{
                int3 offset = int3(offsetX, offsetY, offsetZ);
				int3 neighbourCellWrappedId = int3(i + offset + pointsPerRow) % pointsPerRow;
				uint pointIndex = Flatten3D(neighbourCellWrappedId, pointsPerRow);
				float3 p = GetPoint(pointIndex) + offset;
				minDistSq = min(minDistSq, dot(frc - p, frc - p));
			}
		}
	}
	return sqrt(minDistSq);
}

[numthreads(8, 8, 8)]
void WorleyNoiseCS(uint3 threadId : SV_DISPATCHTHREADID)
{
	float3 uvw = threadId.xyz / (float)cPass.Resolution;

	float4 noiseResult = 0;
	for(uint channel = 0; channel < 4; ++channel)
	{
		uint4 pointsPerRow = cPass.PointsPerRow[channel];
		float persistence = cPass.Persistence[channel];
		uint invertNoise = cPass.InvertNoise[channel];

		for(uint layer = 0; layer < 4; ++layer)
		{
			float noise = saturate((1 - WorleyNoise(uvw, pointsPerRow[layer])) * persistence);
			noiseResult[channel] += noise;
			persistence *= persistence;
		}

		if(invertNoise > 0)
			noiseResult[channel] = 1 - noiseResult[channel];
	}

	uOutputTexture[threadId.xyz] = noiseResult;
}