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
	uint IntAsID;
};

ConstantBuffer<ConstantsData> cConstants : register(b0);
RWStructuredBuffer<uint4> uPickingData : register(u0);

float4 InterpretInt(uint4 value)
{
	uint4 seed = uint4(SeedThread(value.x), SeedThread(value.y), SeedThread(value.z), SeedThread(value.w));
	return float4(Random01(seed.x), Random01(seed.y), Random01(seed.z), Random01(seed.w));
}

float3 UVToDirection(float2 uv, int face)
{
    // Convert UV from [0, 1] to [-1, 1]
    float2 uvScaled = uv * 2.0f - 1.0f;
    
    float3 direction;

    // Map UV coordinates to the correct direction based on the face
    switch(face)
    {
        case 0: // +X
            direction = float3(1.0f, -uvScaled.y, -uvScaled.x);
            break;
        case 1: // -X
            direction = float3(-1.0f, -uvScaled.y, uvScaled.x);
            break;
        case 2: // +Y
            direction = float3(uvScaled.x, 1.0f, uvScaled.y);
            break;
        case 3: // -Y
            direction = float3(uvScaled.x, -1.0f, -uvScaled.y);
            break;
        case 4: // +Z
            direction = float3(uvScaled.x, -uvScaled.y, 1.0f);
            break;
        case 5: // -Z
            direction = float3(-uvScaled.x, -uvScaled.y, -1.0f);
            break;
    }

    // Normalize the resulting direction vector
    return normalize(direction);
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
	else if(cConstants.TextureType == TextureDimension::TexCube)
	{
		TextureCube<T> tex = ResourceDescriptorHeap[cConstants.TextureSource];
		return tex.SampleLevel(sPointClamp, UVToDirection(uv, slice), mip);
	}
	return output;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 threadId : SV_DispatchThreadID)
{
	uint2 texel = threadId.xy;
	if(any(texel >= cConstants.Dimensions))
		return;

	uint4 pickValue = 0;
	float4 rgb;
	float2 uv = TexelToUV(texel, rcp(cConstants.Dimensions));
	if(cConstants.IsIntegerFormat)
	{
		uint4 s = SampleTexture<uint4>(uv);
		rgb = s;
		if(cConstants.IntAsID)
			rgb = InterpretInt(s);
		pickValue = s;
	}
	else
	{
		float4 s = SampleTexture<float4>(uv);
		rgb = s;
		pickValue = asuint(s);
	}

	if(all(texel == cConstants.HoveredPixel))
		uPickingData[0] = pickValue;

	float2 range = cConstants.ValueRange;
	float4 output = 0;
	uint numBits = countbits(cConstants.ChannelMask);
	if(numBits == 1)
	{
		uint singleChannel = firstbithigh(cConstants.ChannelMask);
		output = float4(InverseLerp(rgb[singleChannel], range.x, range.y).xxx, 1);
	}
	else
	{
		output.r = ((cConstants.ChannelMask >> 0) & 1) * InverseLerp(rgb[0], range.x, range.y);
		output.g = ((cConstants.ChannelMask >> 1) & 1) * InverseLerp(rgb[1], range.x, range.y);
		output.b = ((cConstants.ChannelMask >> 2) & 1) * InverseLerp(rgb[2], range.x, range.y);
		output.a = (cConstants.ChannelMask >> 3) & 1 ? rgb.a : 1.0f;
	}

	RWTexture2D<float4> target = ResourceDescriptorHeap[cConstants.TextureTarget];
	target[texel] = output;
}
