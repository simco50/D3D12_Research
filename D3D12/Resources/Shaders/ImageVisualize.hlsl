#include "Common.hlsli"

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
    float2 InvDimensions;
	float2 ValueRange;
	uint TextureSource;
    uint TextureTarget;
	uint TextureType;
	uint ChannelMask;
	float MipLevel;
	float Slice;
};

ConstantBuffer<ConstantsData> cConstants : register(b100);

float4 SampleTexture(float2 uv)
{
	SamplerState samplerState = sPointClamp;
	//float mip = floor(cConstants.MipLevel);
	float mip = cConstants.MipLevel;

	float4 output = float4(1, 0, 1, 1);
	if(cConstants.TextureType == TextureDimension::Tex1D)
	{
		Texture1D tex = ResourceDescriptorHeap[cConstants.TextureSource];
		output = tex.SampleLevel(samplerState, uv.x, mip);
	}
	else if(cConstants.TextureType == TextureDimension::Tex2D)
	{
		Texture2D tex = ResourceDescriptorHeap[cConstants.TextureSource];
		output = tex.SampleLevel(samplerState, uv, mip);
	}
	else if(cConstants.TextureType == TextureDimension::Tex3D)
	{
		Texture3D tex = ResourceDescriptorHeap[cConstants.TextureSource];
		output = tex.SampleLevel(samplerState, float3(uv, cConstants.Slice), mip);
	}
	else if(cConstants.TextureType == TextureDimension::TexCube)
	{
		static const float3x3 CUBEMAP_ROTATIONS[] =
		{
			float3x3(0,0,-1, 0,-1,0, -1,0,0),   // right
			float3x3(0,0,1, 0,-1,0, 1,0,0),     // left
			float3x3(1,0,0, 0,0,-1, 0,1,0),     // top
			float3x3(1,0,0, 0,0,1, 0,-1,0),     // bottom
			float3x3(1,0,0, 0,-1,0, 0,0,-1),    // back
			float3x3(-1,0,0, 0,-1,0, 0,0,1),    // front
		};

		float3 dir = mul(CUBEMAP_ROTATIONS[cConstants.Slice], normalize(float3(uv * 2 - 1, -1)));

		TextureCube tex = ResourceDescriptorHeap[cConstants.TextureSource];
		output = tex.SampleLevel(samplerState, dir, mip);
	}
	return output;
}

[numthreads(16, 16, 1)]
void CSMain(uint3 threadID : SV_DispatchThreadID)
{
    float2 uv = (threadID.xy + 0.5f) * cConstants.InvDimensions;
	RWTexture2D<float4> target = ResourceDescriptorHeap[cConstants.TextureTarget];
	float4 texSample = SampleTexture(uv);

	float4 output = 0;
	uint numBits = countbits(cConstants.ChannelMask);
	if(numBits == 1)
	{
		uint singleChannel = firstbithigh(cConstants.ChannelMask);
		output = float4(InverseLerp(texSample[singleChannel], cConstants.ValueRange.x, cConstants.ValueRange.y).xxx, 1);
	}
	else
	{
		output.r = ((cConstants.ChannelMask >> 0) & 1) * InverseLerp(texSample[0], cConstants.ValueRange.x, cConstants.ValueRange.y);
		output.g = ((cConstants.ChannelMask >> 1) & 1) * InverseLerp(texSample[1], cConstants.ValueRange.x, cConstants.ValueRange.y);
		output.b = ((cConstants.ChannelMask >> 2) & 1) * InverseLerp(texSample[2], cConstants.ValueRange.x, cConstants.ValueRange.y);
		output.a = (cConstants.ChannelMask >> 3) & 1 ? texSample.a : 1.0f;
	}
	target[threadID.xy] = output;
}
