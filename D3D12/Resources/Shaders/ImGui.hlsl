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

struct ConstantsData
{
	float4x4 ViewProj;
	uint TextureID;
	uint TextureType;
};

ConstantBuffer<ConstantsData> cConstants : register(b0);

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

	output.position = mul(float4(input.position.xy, 0.5f, 1.f), cConstants.ViewProj);
	output.color = input.color;
	output.texCoord = input.texCoord;

	return output;
}

float4 SampleTexture(float2 texCoord)
{
	if(cConstants.TextureType == 2)
	{
		return tTexture2DTable[cConstants.TextureID].SampleLevel(sMaterialSampler, texCoord, 0);
	}
	if(cConstants.TextureType == 4)
	{
		float4 c = tTexture3DTable[cConstants.TextureID].SampleLevel(sMaterialSampler, float3(texCoord, 0), 0);
		return float4(c.xyz, 1.0f);
	}
	if(cConstants.TextureType == 5)
	{
		float4 c = tTextureCubeTable[cConstants.TextureID].SampleLevel(sMaterialSampler, float3(texCoord, 0), 0);
		return float4(c.xyz, 1.0f);
	}
	return float4(1, 0, 1, 1);
}

float4 PSMain(PS_INPUT input) : SV_TARGET
{
	return input.color * SampleTexture(input.texCoord);
}