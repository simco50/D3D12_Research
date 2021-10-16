#include "CommonBindings.hlsli"

#define RootSig ROOT_SIG("RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
				"CBV(b0, visibility=SHADER_VISIBILITY_VERTEX)")

cbuffer Parameters : register(b0)
{
	float4x4 cViewProj;
}

struct VS_INPUT
{
	float3 position : POSITION;
	uint color : COLOR;
};

struct PS_INPUT
{
	float4 position : SV_POSITION;
	float4 color : COLOR;
};

[RootSignature(RootSig)]
PS_INPUT VSMain(VS_INPUT input)
{
	PS_INPUT output = (PS_INPUT)0;

	output.position = mul(float4(input.position, 1.0f), cViewProj);
	output.color = UIntToColor(input.color);

	return output;
}

float4 PSMain(PS_INPUT input) : SV_TARGET
{
	return input.color;
}