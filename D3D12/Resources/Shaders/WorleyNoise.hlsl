
#define RootSig "CBV(b0, visibility=SHADER_VISIBILITY_ALL), " \
				"DescriptorTable(UAV(u0, numDescriptors = 1), visibility=SHADER_VISIBILITY_ALL), " \

cbuffer Constants : register(b0)
{
	float4 cPoints[1024];
	uint4 cPointsPerRow[4];
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
				int pointIndex = neighbourCellWrappedId.x + pointsPerRow * (neighbourCellWrappedId.y + neighbourCellWrappedId.z * pointsPerRow);
				float3 p = cPoints[pointIndex % 1024].xyz + offset;
				minDistSq = min(minDistSq, dot(frc - p, frc - p));
			}
		}
	}
	return sqrt(minDistSq);
}

[RootSignature(RootSig)]
[numthreads(8, 8, 8)]
void WorleyNoiseCS(uint3 threadId : SV_DISPATCHTHREADID)
{
	float3 uvw = threadId.xyz / (float)Resolution;
	float4 output = 0;
	for(int i = 0; i < 4; ++i)
	{
		float r = WorleyNoise(uvw, cPointsPerRow[i].x);
		float g = WorleyNoise(uvw, cPointsPerRow[i].y);
		float b = WorleyNoise(uvw, cPointsPerRow[i].z);
		float a = WorleyNoise(uvw, cPointsPerRow[i].a);
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