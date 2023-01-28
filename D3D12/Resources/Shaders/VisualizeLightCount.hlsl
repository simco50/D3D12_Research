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

static int2 SobelWeights[] = {
	int2(1,  1), int2(0,  2), int2(-1,  1),
	int2(2,  0), int2(0,  0), int2(-2,  0),
	int2(1, -1), int2(0, -2), int2(-1, -1),
};

static int2 SobelOffsets[] = {
	int2(-1,  1), int2(0,  1), int2(1,  1),
	int2(-1,  0), int2(0,  0), int2(1,  0),
	int2(-1, -1), int2(0, -1), int2(1, -1),
};

float3 ApplyEdgeDetection(uint2 pixel, float3 color)
{
	float2 depthGrad = 0.0f;
	for(uint i = 0; i < ArraySize(SobelWeights); ++i)
	{
		float linearDepth = LinearizeDepth(tSceneDepth.Load(uint3(pixel + SobelOffsets[i], 0)));
		float logDepth = log2(linearDepth + 1.0f) * 5.0f;
		depthGrad += SobelWeights[i] * logDepth;
	}
	float edge = saturate(MaxComponent(abs(depthGrad)));
	return color * (1.0f - edge * 0.5f);
}

float3 GetColor(uint2 pixel, uint lightCount)
{
	float3 color = Inferno(saturate(0.1f *  lightCount));
	return ApplyEdgeDetection(pixel, color);
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
	uOutput[threadId.xy] = float4(GetColor(threadId.xy, lightCount), 1);

}
