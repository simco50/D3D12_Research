#include "CommonBindings.hlsli"

#define RootSig ROOT_SIG("RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
				"CBV(b0, visibility=SHADER_VISIBILITY_ALL)")

#define Texture1D 0
#define Texture1DArray 1
#define Texture2D 2
#define Texture2DArray 3
#define Texture3D 4
#define TextureCube 5
#define TextureCubeArray 6

cbuffer Data : register(b0)
{
	float4x4 cViewProj;
	uint TextureID;
	uint TextureType;
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

[RootSignature(RootSig)]
PS_INPUT VSMain(VS_INPUT input)
{
	PS_INPUT output = (PS_INPUT)0;

	output.position = mul(float4(input.position.xy, 0.5f, 1.f), cViewProj);
	output.color = input.color;
	output.texCoord = input.texCoord;

	return output;
}

float4 SampleTexture(float2 texCoord)
{
	if(TextureType == 2)
	{
		return tTexture2DTable[TextureID].SampleLevel(sMaterialSampler, texCoord, 0);
	}
	if(TextureType == 4)
	{
		float4 c = tTexture3DTable[TextureID].SampleLevel(sMaterialSampler, float3(texCoord, 0), 0);
		return float4(c.xyz, 1.0f);
	}
	if(TextureType == 5)
	{
		float4 c = tTextureCubeTable[TextureID].SampleLevel(sMaterialSampler, float3(texCoord, 0), 0);
		return float4(c.xyz, 1.0f);
	}
	return float4(1, 0, 1, 1);
}

float4 PSMain(PS_INPUT input) : SV_TARGET
{
	return input.color * SampleTexture(input.texCoord);
}