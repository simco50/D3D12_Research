#include "Common.hlsli"

#define RootSig "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
				"CBV(b0, visibility=SHADER_VISIBILITY_PIXEL), " \
				"CBV(b1, visibility=SHADER_VISIBILITY_VERTEX), " \
				"DescriptorTable(SRV(t0, numDescriptors = 1), visibility = SHADER_VISIBILITY_PIXEL), " \
				"DescriptorTable(SRV(t0, numDescriptors = 1, space=1), visibility = SHADER_VISIBILITY_PIXEL), " \
				"StaticSampler(s0, filter=FILTER_MIN_MAG_MIP_POINT, visibility = SHADER_VISIBILITY_PIXEL)"

cbuffer Data : register(b1)
{
	float4x4 cViewProj;
}

cbuffer PerBatchData : register(b0)
{
	uint cVisibleChannels;
	int cUniqueChannel;
	int cMipLevel;
	float cSliceIndex;
	int cTextureType;
}

struct VS_INPUT
{
	float2 position : POSITION;
	float2 texCoord  : TEXCOORD0;
	float4 color : COLOR0;
};

struct PS_INPUT
{
	float4 position : SV_POSITION;
	float2 texCoord  : TEXCOORD0;
	float4 color : COLOR0;
};

SamplerState sDiffuse : register(s0);
Texture2D tDiffuse2D : register(t0, space0);
Texture3D tDiffuse3D : register(t0, space1);

[RootSignature(RootSig)]
PS_INPUT VSMain(VS_INPUT input)
{
	PS_INPUT output = (PS_INPUT)0;

	output.position = mul(float4(input.position.xy, 0.5f, 1.f), cViewProj);
	output.color = input.color;
	output.texCoord = input.texCoord;
	return output;
}

float4 PSMain(PS_INPUT input) : SV_TARGET
{
	float4 color = 0;
	if(cTextureType == TEXTURE_2D)
	{
		color = input.color * tDiffuse2D.SampleLevel(sDiffuse, input.texCoord, cMipLevel);
	}
	else if (cTextureType == TEXTURE_3D)
	{
		color = input.color * tDiffuse3D.SampleLevel(sDiffuse, float3(input.texCoord, cSliceIndex), cMipLevel);
	}

	float4 outColor = 0;
	if(cUniqueChannel < 0)
	{
		outColor.r = color.r * ((cVisibleChannels & (1 << 0)) > 0);
		outColor.g = color.g * ((cVisibleChannels & (1 << 1)) > 0);
		outColor.b = color.b * ((cVisibleChannels & (1 << 2)) > 0);
		outColor.a = lerp(1, color.a, ((cVisibleChannels & (1 << 3)) > 0));
	}
	else
	{
		outColor.r = color[cUniqueChannel];
		outColor.g = color[cUniqueChannel];
		outColor.b = color[cUniqueChannel];
		outColor.a = 1;
	}
	return outColor;
}

//Swizzle R G B
//Visibility RGBA
//0001 0001 0010 1111