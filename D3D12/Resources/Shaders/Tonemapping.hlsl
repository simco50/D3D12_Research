#include "Common.hlsli"
#include "TonemappingCommon.hlsli"
#include "Color.hlsli"

#define TONEMAP_REINHARD 0
#define TONEMAP_REINHARD_EXTENDED 1
#define TONEMAP_ACES_FAST 2
#define TONEMAP_UNREAL3 3
#define TONEMAP_UNCHARTED2 4

#define BLOCK_SIZE 16

#define RootSig "CBV(b0, visibility=SHADER_VISIBILITY_ALL), " \
				"DescriptorTable(UAV(u0, numDescriptors = 1), visibility=SHADER_VISIBILITY_ALL), " \
				"DescriptorTable(SRV(t0, numDescriptors = 2), visibility=SHADER_VISIBILITY_ALL), " \

cbuffer Parameters : register(b0)
{
	float cWhitePoint;
	uint cTonemapper;
}

RWTexture2D<float4> uOutColor : register(u0);
Texture2D tColor : register(t0);
StructuredBuffer<float> tAverageLuminance : register(t1);

[RootSignature(RootSig)]
[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void CSMain(uint3 dispatchThreadId : SV_DISPATCHTHREADID)
{
	uint2 dimensions;
	tColor.GetDimensions(dimensions.x, dimensions.y);
	if(dispatchThreadId.x >= dimensions.x || dispatchThreadId.y >= dimensions.y)
	{
		return;
	}

	float3 rgb = tColor.Load(uint3(dispatchThreadId.xy, 0)).rgb;

#if TONEMAP_LUMINANCE
    float3 xyY = sRGB_to_xyY(rgb);
	float value = xyY.z;
#else
	float3 value = rgb;
#endif

    float exposure = tAverageLuminance[2];
	value = value * exposure;

	switch(cTonemapper)
	{
	case TONEMAP_REINHARD:
    	value = Reinhard(value);
		break;
	case TONEMAP_REINHARD_EXTENDED:
    	value = ReinhardExtended(value, cWhitePoint);
		break;
	case TONEMAP_ACES_FAST:
		value = ACES_Fast(value);
		break;
	case TONEMAP_UNREAL3:
		value = Unreal3(value);
		break;
	case TONEMAP_UNCHARTED2:
		value = Uncharted2(value);
		break;
	}

#if TONEMAP_LUMINANCE
	xyY.z = value;
	rgb = xyY_to_sRGB(xyY);
#else
	rgb = value;
#endif

	if(cTonemapper != TONEMAP_UNREAL3)
	{
		rgb = LinearToSrgbFast(rgb);
	}
	uOutColor[dispatchThreadId.xy] = float4(rgb, 1);
}