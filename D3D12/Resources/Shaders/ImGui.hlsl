#include "Common.hlsl"

#define RootSig "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
				"CBV(b0, visibility=SHADER_VISIBILITY_VERTEX), " \
				"DescriptorTable(SRV(t0, numDescriptors = 1), visibility = SHADER_VISIBILITY_PIXEL), " \
				"StaticSampler(s0, filter=FILTER_MIN_MAG_MIP_POINT, visibility = SHADER_VISIBILITY_PIXEL)"

cbuffer Data : register(b0)
{
	float4x4 cViewProj;
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
Texture2D tDiffuse : register(t0);

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
	return input.color* tDiffuse.Sample(sDiffuse, input.texCoord);
}