#include "Common.hlsli"
#include "Random.hlsli"

enum TextureDimension : uint
{
	Tex1D,
	Tex1DArray,
	Tex2D,
	Tex2DArray,
	Tex3D,
	TexCube,
	TexCubeArray,
};

struct PickingData
{
	float4 DataFloat;
	uint4  DataUInt;
};

struct ConstantsData
{
	uint2 HoveredPixel;
    uint2 Dimensions;
	float2 ValueRange;
	uint TextureSource;
    uint TextureTarget;
	TextureDimension TextureType;
	uint ChannelMask;
	uint MipLevel;
	uint Slice;
	uint IsIntegerFormat;
};

ConstantBuffer<ConstantsData> cConstants : register(b0);
RWStructuredBuffer<PickingData> uPickingData : register(u0);

float4 InterpretInt(uint4 value)
{
	uint4 seed = uint4(SeedThread(value.x), SeedThread(value.y), SeedThread(value.z), SeedThread(value.w));
	return float4(Random01(seed.x), Random01(seed.y), Random01(seed.z), Random01(seed.w));
}

template<typename T>
T SampleTexture(float2 uv)
{
	SamplerState samplerState = sPointClamp;
	uint mip = cConstants.MipLevel;
	uint slice = cConstants.Slice;

	T output = T(1, 0, 1, 1);
	if(cConstants.TextureType == TextureDimension::Tex1D)
	{
		Texture1D<T> tex = ResourceDescriptorHeap[cConstants.TextureSource];
		return tex.SampleLevel(sPointClamp, uv.x, mip);
	}
	else if(cConstants.TextureType == TextureDimension::Tex2D)
	{
		Texture2D<T> tex = ResourceDescriptorHeap[cConstants.TextureSource];
		return tex.SampleLevel(sPointClamp, uv, mip);
	}
	else if(cConstants.TextureType == TextureDimension::Tex3D)
	{
		Texture3D<T> tex = ResourceDescriptorHeap[cConstants.TextureSource];
		return tex.SampleLevel(sPointClamp, float3(uv, slice), mip);
	}
	return output;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 threadId : SV_DispatchThreadID)
{
	uint2 texel = threadId.xy;
	if(any(texel >= cConstants.Dimensions))
		return;

	PickingData data;
	float4 value;
	float2 uv = TexelToUV(texel, rcp(cConstants.Dimensions));
	if(cConstants.IsIntegerFormat)
	{
		data.DataUInt = SampleTexture<uint4>(uv);
		value = InterpretInt(data.DataUInt);
	}
	else
	{
		data.DataFloat = SampleTexture<float4>(uv);
		value = data.DataFloat;
	}

	if(all(texel == cConstants.HoveredPixel))
		uPickingData[0] = data;

	float4 texSample = value;
	float2 range = cConstants.ValueRange;

	float4 output = 0;
	uint numBits = countbits(cConstants.ChannelMask);
	if(numBits == 1)
	{
		uint singleChannel = firstbithigh(cConstants.ChannelMask);
		output = float4(InverseLerp(texSample[singleChannel], range.x, range.y).xxx, 1);
	}
	else
	{
		output.r = ((cConstants.ChannelMask >> 0) & 1) * InverseLerp(texSample[0], range.x, range.y);
		output.g = ((cConstants.ChannelMask >> 1) & 1) * InverseLerp(texSample[1], range.x, range.y);
		output.b = ((cConstants.ChannelMask >> 2) & 1) * InverseLerp(texSample[2], range.x, range.y);
		output.a = (cConstants.ChannelMask >> 3) & 1 ? texSample.a : 1.0f;
	}

	RWTexture2D<float4> target = ResourceDescriptorHeap[cConstants.TextureTarget];
	target[texel] = output;
}
