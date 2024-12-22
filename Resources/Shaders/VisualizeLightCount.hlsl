#include "Common.hlsli"
#include "ColorMaps.hlsli"
#include "ShaderDebugRender.hlsli"
#include "DebugFont.hlsli"

struct PassParameters
{
	int2 ClusterDimensions;
	int2 ClusterSize;
	float2 LightGridParams;
};

struct Params
{
	float3 ViewMin;
	float padding;
	float3 ViewMax;
};

ConstantBuffer<Params> cParams : register(b0);
ConstantBuffer<PassParameters> cPass : register(b1);

RWTexture2D<float4> uOutput : register(u0);

Texture2D<float> tDepth : register(t0);
Buffer<uint> tLightGrid : register(t1);

static const int MaxNumLights = 32;

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
		float linearDepth = LinearizeDepth(tDepth.Load(uint3(pixel + SobelOffsets[i], 0)));
		float logDepth = log2(linearDepth + 1.0f) * 5.0f;
		depthGrad += SobelWeights[i] * logDepth;
	}
	float edge = saturate(MaxComponent(abs(depthGrad)));
	return color * (1.0f - edge * 0.5f);
}

float3 GetColor(uint2 pixel, uint lightCount)
{
	float3 color = Turbo(saturate((float)lightCount / MaxNumLights));
	return ApplyEdgeDetection(pixel, color);
}

uint GetLightCount(uint2 threadId, float viewDepth, out uint2 tileLocation, out uint2 tileSize)
{
#if TILED_FORWARD
	uint2 tileIndex = uint2(floor(threadId.xy / TILED_LIGHTING_TILE_SIZE));
	uint tileIndex1D = tileIndex.x + DivideAndRoundUp(cView.ViewportDimensions.x, TILED_LIGHTING_TILE_SIZE) * tileIndex.y;
	uint lightGridOffset = tileIndex1D * TILED_LIGHTING_NUM_BUCKETS;
	uint lightCount = 0;
	for(uint i = 0; i < TILED_LIGHTING_NUM_BUCKETS; ++i)
		lightCount += countbits(tLightGrid[lightGridOffset + i]);

	tileLocation = (tileIndex + 0.5f) * TILED_LIGHTING_TILE_SIZE;
	tileSize = TILED_LIGHTING_TILE_SIZE;

#elif CLUSTERED_FORWARD
	uint slice = floor(log(viewDepth) * cPass.LightGridParams.x - cPass.LightGridParams.y);
	uint3 clusterIndex3D = uint3(floor(threadId.xy / cPass.ClusterSize), slice);
	uint clusterIndex1D = clusterIndex3D.x + (cPass.ClusterDimensions.x * (clusterIndex3D.y + cPass.ClusterDimensions.y * clusterIndex3D.z));
	uint lightGridOffset = clusterIndex1D * CLUSTERED_LIGHTING_NUM_BUCKETS;
	uint lightCount = 0;
	for(uint i = 0; i < CLUSTERED_LIGHTING_NUM_BUCKETS; ++i)
		lightCount += countbits(tLightGrid[lightGridOffset + i]);

	tileLocation = (clusterIndex3D.xy + 0.5f) * cPass.ClusterSize.xy;
	tileSize = cPass.ClusterSize.xy;
#endif

	return lightCount;
}

[numthreads(8, 8, 1)]
void DebugLightDensityCS(uint3 threadId : SV_DispatchThreadID)
{
	if(any(threadId.xy >= cView.ViewportDimensions))
		return;

	float viewDepth = 0;
#ifdef CLUSTERED_FORWARD
	float depth = tDepth.Load(uint3(threadId.xy, 0));
	viewDepth = LinearizeDepth(depth, cView.NearZ, cView.FarZ);
#endif

	uint2 tileLocation;
	uint2 tileSize;
	uint lightCount = GetLightCount(threadId.xy, viewDepth, tileLocation, tileSize);

	// Draw legend
	const float boxSize = 26;
	if(threadId.x < MaxNumLights && threadId.y == 0)
	{
		float2 cursor = float2(5 + threadId.x * boxSize, 5);
		TextWriter writer = CreateTextWriter(cursor + 0.2f * boxSize);
		writer.Int(threadId.x);
	}

	float4 color;
	uint2 edge = threadId.xy % tileSize == 0;
	float2 boxPos = ((int2)threadId.xy - 5) / float2(boxSize * MaxNumLights, boxSize);
	if(all(boxPos >= 0) && all(boxPos <= 1))
		color = float4(Turbo(boxPos.x), 1);
	else if(any(edge))
		color = 0;
	else
		color = float4(GetColor(threadId.xy, lightCount), 1);

	uOutput[threadId.xy] = color;
}


float4 TopDownViewPS(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target0
{
	float3 viewPos = lerp(cParams.ViewMin, cParams.ViewMax, float3(uv.x, 0.5f, uv.y));
	float4 projPos = mul(float4(viewPos, 1.0f), cView.ViewToClip);
	projPos.xyz /= projPos.w;
	projPos.y *= -1.0f;
	projPos.xy = projPos.xy * 0.5f + 0.5f;

	if(any(projPos.xy < 0.0f) || any(projPos.xy > 1.0f))
		return 0;

	float2 screenPos = projPos.xy * cView.ViewportDimensions;
	float viewDepth = saturate(viewPos.z - cView.FarZ) / (cView.NearZ - cView.FarZ);

	uint2 tileLocation;
	uint2 tileSize;
	uint lightCount = GetLightCount(floor(screenPos), viewPos.z, tileLocation, tileSize);

	return float4(Turbo(saturate((float)lightCount / MaxNumLights)), 1);
}
