#include "CommonBindings.hlsli"

#define BLOCK_SIZE 16

struct PassParameters
{
	int2 ClusterDimensions;
	int2 ClusterSize;
	float2 LightGridParams;
};

ConstantBuffer<PassParameters> cPass : register(b0);
Texture2D<float4> tInput : register(t0);
Texture2D<float> tSceneDepth : register(t1);
RWTexture2D<float4> uOutput : register(u0);

#if TILED_FORWARD
Texture2D<uint2> tLightGrid : register(t2);
#elif CLUSTERED_FORWARD
StructuredBuffer<uint2> tLightGrid : register(t2);
#endif

static float4 DEBUG_COLORS[] = {
	float4(0,4,141, 255) / 255,
	float4(5,10,255, 255) / 255,
	float4(0,164,255, 255) / 255,
	float4(0,255,189, 255) / 255,
	float4(0,255,41, 255) / 255,
	float4(117,254,1, 255) / 255,
	float4(255,239,0, 255) / 255,
	float4(255,86,0, 255) / 255,
	float4(204,3,0, 255) / 255,
	float4(65,0,1, 255) / 255,
};

float EdgeDetection(uint2 index, uint width, uint height)
{
	float reference = LinearizeDepth01(tSceneDepth.Load(uint3(index, 0)), cView.NearZ, cView.FarZ);
	uint2 offsets[8] = {
		uint2(-1, -1),
		uint2(-1, 0),
		uint2(-1, 1),
		uint2(0, -1),
		uint2(0, 1),
		uint2(1, -1),
		uint2(1, 0),
		uint2(1, 1)
	};
	float sampledValue = 0;
	for(int j = 0; j < 8; j++)
	{
		sampledValue += LinearizeDepth01(tSceneDepth.Load(uint3(index + offsets[j], 0)), cView.NearZ, cView.FarZ);
	}
	sampledValue /= 8;
	return lerp(1, 0, step(0.0002f, length(reference - sampledValue)));
}

float InverseLerp(float value, float minValue, float maxValue)
{
	return (value - minValue) / (maxValue - minValue);
}

[numthreads(16, 16, 1)]
void DebugLightDensityCS(uint3 threadId : SV_DispatchThreadID)
{
	uint width, height;
	tInput.GetDimensions(width, height);
	if(threadId.x < width && threadId.y < height)
	{

#if TILED_FORWARD
		uint2 tileIndex = uint2(floor(threadId.xy / BLOCK_SIZE));
		uint lightCount = tLightGrid[tileIndex].y;
		uOutput[threadId.xy] = EdgeDetection(threadId.xy, width, height) * DEBUG_COLORS[min(9, lightCount)];
#elif CLUSTERED_FORWARD

#if 0
		float fov = cFoV / 2.0;
		float height = 300.0;

		int2 pos = threadId.xy;
		pos.y = (720 - pos.y);
		pos -= uint2(1240, 720) / 2.0;
		pos.y += 150.0;

		float angle = atan2(pos.x, pos.y);
		if(angle > -fov && angle < fov && pos.y < height)
		{
			float angleNormalized = InverseLerp(angle, -fov, fov);
			int widthSlice = floor(angleNormalized * cPass.ClusterDimensions.x);

			float viewDepth = (float)pos.y / height * (cView.NearZ - cView.FarZ) + cView.FarZ;
			uint slice = floor(log(viewDepth) * cPass.LightGridParams.x - cPass.LightGridParams.y);
			uint lightCount = 0;
			for(uint i = 0; i < cPass.ClusterDimensions.y; ++i)
			{
				uint3 clusterIndex3D = uint3(widthSlice, i, slice);
				uint clusterIndex1D = clusterIndex3D.x + (cPass.ClusterDimensions.x * (clusterIndex3D.y + cPass.ClusterDimensions.y * clusterIndex3D.z));
				lightCount = max(lightCount, tLightGrid[clusterIndex1D].y);
			}
			uOutput[threadId.xy] = float4(DEBUG_COLORS[min(9, lightCount)]);
		}
		else
#endif
		{

			float depth = tSceneDepth.Load(uint3(threadId.xy, 0));
			float viewDepth = LinearizeDepth(depth, cView.NearZ, cView.FarZ);
			uint slice = floor(log(viewDepth) * cPass.LightGridParams.x - cPass.LightGridParams.y);
			uint3 clusterIndex3D = uint3(floor(threadId.xy / cPass.ClusterSize), slice);
			uint clusterIndex1D = clusterIndex3D.x + (cPass.ClusterDimensions.x * (clusterIndex3D.y + cPass.ClusterDimensions.y * clusterIndex3D.z));
			uint lightCount = tLightGrid[clusterIndex1D].y;
			uOutput[threadId.xy] = EdgeDetection(threadId.xy, width, height) * DEBUG_COLORS[min(9, lightCount)];
		}
#endif
	}
}
