#include "Common.hlsli"
#include "ColorMaps.hlsli"
#include "ShaderDebugRender.hlsli"

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
Buffer<uint> tLightGrid : register(t2);
#endif

static const int MaxNumLights = 10;

static const int2 SobelWeights[] = {
	int2(1,  1), int2(0,  2), int2(-1,  1),
	int2(2,  0), int2(0,  0), int2(-2,  0),
	int2(1, -1), int2(0, -2), int2(-1, -1),
};

static const int2 SobelOffsets[] = {
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
	float3 color = Inferno(saturate((float)lightCount / MaxNumLights));
	return ApplyEdgeDetection(pixel, color);
}

[numthreads(16, 16, 1)]
void DebugLightDensityCS(uint3 threadId : SV_DispatchThreadID)
{
	if(any(threadId.xy >= cView.TargetDimensions))
		return;

#if TILED_FORWARD
	uint2 tileIndex = uint2(floor(threadId.xy / TILED_LIGHTING_TILE_SIZE));
	uint lightCount = tLightGrid[tileIndex].y;
#elif CLUSTERED_FORWARD
	float depth = tSceneDepth.Load(uint3(threadId.xy, 0));
	float viewDepth = LinearizeDepth(depth, cView.NearZ, cView.FarZ);
	uint slice = floor(log(viewDepth) * cPass.LightGridParams.x - cPass.LightGridParams.y);
	uint3 clusterIndex3D = uint3(floor(threadId.xy / cPass.ClusterSize), slice);
	uint clusterIndex1D = clusterIndex3D.x + (cPass.ClusterDimensions.x * (clusterIndex3D.y + cPass.ClusterDimensions.y * clusterIndex3D.z));
	uint lightCount = tLightGrid[clusterIndex1D];
#endif
	uOutput[threadId.xy] = float4(GetColor(threadId.xy, lightCount), 1);

	// Draw legend
	const float boxSize = 40;
	const float2 topLeft = cView.ViewportDimensions - float2(boxSize + 10, boxSize * MaxNumLights + 10);

	if(threadId.x < MaxNumLights && threadId.y == 0)
	{
		float2 cursor = float2(topLeft.x, topLeft.y + threadId.x * boxSize);
		DrawRect(topLeft * cView.ViewportDimensionsInv, (cursor + boxSize) * cView.ViewportDimensionsInv, RectMode::MinMax);
		TextWriter writer = CreateTextWriter(cursor + 0.2f * boxSize);
		writer.Int(threadId.x);
	}

	float2 boxPos = ((int2)threadId.xy - topLeft) / float2(boxSize, boxSize * MaxNumLights);
	if(all(boxPos >= 0) && all(boxPos <= 1))
	{
		uOutput[threadId.xy] = float4(Inferno(boxPos.y), 1);
	}
}
