#include "Common.hlsli"

struct PassParameters
{
	float4 Points[1024];
	uint4 PointsPerRow[4];
	uint Resolution;
};

ConstantBuffer<PassParameters> cPass : register(b0);
RWTexture3D<float4> uOutputTexture : register(u0);

float WorleyNoise(float3 uvw, uint pointsPerRow)
{
	uvw *= pointsPerRow;
	float3 frc = frac(uvw);
	uint3 i = floor(uvw);

	float minDistSq = 1;
	[loop]
	for (int offsetZ = -1; offsetZ <= 1; ++offsetZ)
	{
		[loop]
		for (int offsetY = -1; offsetY <= 1; ++offsetY)
		{
			[loop]
			for (int offsetX = -1; offsetX <= 1; ++offsetX)
			{
                int3 offset = int3(offsetX, offsetY, offsetZ);
				int3 neighbourCellWrappedId = int3(i + offset + pointsPerRow) % pointsPerRow;
				int pointIndex = neighbourCellWrappedId.x + pointsPerRow * (neighbourCellWrappedId.y + neighbourCellWrappedId.z * pointsPerRow);
				float3 p = cPass.Points[pointIndex % 1024].xyz + offset;
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
	float4 output = 0;
	for(int i = 0; i < 4; ++i)
	{
		float r = WorleyNoise(uvw, cPass.PointsPerRow[i].x);
		float g = WorleyNoise(uvw, cPass.PointsPerRow[i].y);
		float b = WorleyNoise(uvw, cPass.PointsPerRow[i].z);
		float a = WorleyNoise(uvw, cPass.PointsPerRow[i].a);
		float4 total = float4(r,g,b,a);
		float persistence = 0.5f;
		for(int j = 0; j < 4; ++j)
		{
			output[i] += total[j] * persistence;
			persistence *= persistence;
		}
	}
	uOutputTexture[threadId.xyz] = output;
}