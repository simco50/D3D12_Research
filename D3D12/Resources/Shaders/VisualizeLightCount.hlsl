#include "Common.hlsli"
#include "ColorMaps.hlsli"

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

float EdgeDetection(uint2 pixel)
{
	float reference = LinearizeDepth(tSceneDepth.Load(uint3(pixel, 0)));
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
		sampledValue += LinearizeDepth(tSceneDepth.Load(uint3(pixel + offsets[j], 0)));
	}
	sampledValue /= 8;
	return lerp(1, 0, step(0.05f, length(reference - sampledValue)));
}

float4 GetColor(uint2 pixel, uint lightCount)
{
	float edge = EdgeDetection(pixel);
	float3 color = Magma(saturate(0.1f *  lightCount));
	return float4(edge * color, 1.0f);
}

[numthreads(16, 16, 1)]
void DebugLightDensityCS(uint3 threadId : SV_DispatchThreadID)
{
	if(any(threadId.xy >= cView.TargetDimensions))
		return;

#if TILED_FORWARD
	uint2 tileIndex = uint2(floor(threadId.xy / BLOCK_SIZE));
	uint lightCount = tLightGrid[tileIndex].y;
#elif CLUSTERED_FORWARD
	float depth = tSceneDepth.Load(uint3(threadId.xy, 0));
	float viewDepth = LinearizeDepth(depth, cView.NearZ, cView.FarZ);
	uint slice = floor(log(viewDepth) * cPass.LightGridParams.x - cPass.LightGridParams.y);
	uint3 clusterIndex3D = uint3(floor(threadId.xy / cPass.ClusterSize), slice);
	uint clusterIndex1D = clusterIndex3D.x + (cPass.ClusterDimensions.x * (clusterIndex3D.y + cPass.ClusterDimensions.y * clusterIndex3D.z));
	uint lightCount = tLightGrid[clusterIndex1D].y;
#endif
	uOutput[threadId.xy] = GetColor(threadId.xy, lightCount);

}
