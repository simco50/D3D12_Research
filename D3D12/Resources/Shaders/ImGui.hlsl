#include "CommonBindings.hlsli"

struct ConstantsData
{
	float4x4 ViewProj;
	uint TextureID;
	uint TextureType;
};

ConstantBuffer<ConstantsData> cConstants : register(b0);

struct VS_INPUT
{
	float2 Position : POSITION;
	float2 UV : TEXCOORD0;
	float4 Color : COLOR0;
};

struct PS_INPUT
{
	float4 Position : SV_POSITION;
	float2 UV : TEXCOORD0;
	float4 Color : COLOR0;
};

PS_INPUT VSMain(VS_INPUT input)
{
	PS_INPUT output = (PS_INPUT)0;

	output.Position = mul(float4(input.Position.xy, 0.5f, 1.f), cConstants.ViewProj);
	output.Color = input.Color;
	output.UV = input.UV;

	return output;
}

float4 PSMain(PS_INPUT input) : SV_TARGET
{
	float4 texSample = tTexture2DTable[cConstants.TextureID].SampleLevel(sMaterialSampler, input.UV, 0);
	return input.Color * texSample;
}
