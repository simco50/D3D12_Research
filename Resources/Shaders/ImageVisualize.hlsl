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

struct PassParams
{
	uint2 HoveredPixel;
    uint2 Dimensions;
	float2 ValueRange;
	DescriptorHandleBase TextureSource;
    RWTexture2DH<float4> TextureTarget;
	TextureDimension TextureType;
	uint ChannelMask;
	uint MipLevel;
	uint Slice;
	uint IsIntegerFormat;
	uint IntAsID;
	RWStructuredBufferH<uint4> PickingData;
};
DEFINE_CONSTANTS(PassParams, 0);

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
	PassParams passParams = cPassParams;
	SamplerState samplerState = sPointClamp;
	uint mip = passParams.MipLevel;
	uint slice = passParams.Slice;

	T output = T(1, 0, 1, 1);
	if(passParams.TextureType == TextureDimension::Tex1D)
	{
		Texture1D<T> tex = ResourceDescriptorHeap[passParams.TextureSource.Index];
		return tex.SampleLevel(sPointClamp, uv.x, mip);
	}
	else if(passParams.TextureType == TextureDimension::Tex2D)
	{
		Texture2D<T> tex = ResourceDescriptorHeap[passParams.TextureSource.Index];
		return tex.SampleLevel(sPointClamp, uv, mip);
	}
	else if(passParams.TextureType == TextureDimension::Tex3D)
	{
		Texture3D<T> tex = ResourceDescriptorHeap[passParams.TextureSource.Index];
		return tex.SampleLevel(sPointClamp, float3(uv, slice), mip);
	}
	else if(passParams.TextureType == TextureDimension::TexCube)
	{
		TextureCube<T> tex = ResourceDescriptorHeap[passParams.TextureSource.Index];
		return tex.SampleLevel(sPointClamp, UVToDirection(uv, slice), mip);
	}
	return output;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 threadId : SV_DispatchThreadID)
{
	PassParams passParams = cPassParams;
	uint2 texel = threadId.xy;
	if(any(texel >= passParams.Dimensions))
		return;

	uint4 pickValue = 0;
	float4 rgb;
	float2 uv = TexelToUV(texel, rcp(passParams.Dimensions));
	if(passParams.IsIntegerFormat)
	{
		uint4 s = SampleTexture<uint4>(uv);
		rgb = s;
		if(passParams.IntAsID)
			rgb = InterpretInt(s);
		pickValue = s;
	}
	else
	{
		float4 s = SampleTexture<float4>(uv);
		rgb = s;
		pickValue = asuint(s);
	}

	if(all(texel == passParams.HoveredPixel))
		passParams.PickingData.Store(0, pickValue);

	float2 range = passParams.ValueRange;
	float4 output = 0;
	uint numBits = countbits(passParams.ChannelMask);
	if(numBits == 1)
	{
		uint singleChannel = firstbithigh(passParams.ChannelMask);
		output = float4(InverseLerp(rgb[singleChannel], range.x, range.y).xxx, 1);
	}
	else
	{
		output.r = ((passParams.ChannelMask >> 0) & 1) * InverseLerp(rgb[0], range.x, range.y);
		output.g = ((passParams.ChannelMask >> 1) & 1) * InverseLerp(rgb[1], range.x, range.y);
		output.b = ((passParams.ChannelMask >> 2) & 1) * InverseLerp(rgb[2], range.x, range.y);
		output.a = (passParams.ChannelMask >> 3) & 1 ? rgb.a : 1.0f;
	}

	passParams.TextureTarget.Store(texel, output);
}
