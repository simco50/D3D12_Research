cbuffer Constants : register(b0)
{
	float4 cPoints[256];
	uint4 cPointsPerRow;
	uint Resolution;
}

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
				float3 p = cPoints[neighbourCellWrappedId.x + pointsPerRow * (neighbourCellWrappedId.y + neighbourCellWrappedId.z * pointsPerRow)].xyz + offset;
				minDistSq = min(minDistSq, dot(frc - p, frc - p));
			}
		}
	}
	return sqrt(minDistSq);
}

[numthreads(8, 8, 8)]
void WorleyNoiseCS(uint3 threadId : SV_DISPATCHTHREADID)
{
	float3 uvw = threadId.xyz / (float)Resolution;
	float r = WorleyNoise(uvw, cPointsPerRow.x);
	float g = WorleyNoise(uvw, cPointsPerRow.y);
	float b = WorleyNoise(uvw, cPointsPerRow.z);
	float combined = r * 0.625f + g * 0.225f + b * 0.150f;
	uOutputTexture[threadId.xyz] = float4(combined, 0, 0, 1);
}