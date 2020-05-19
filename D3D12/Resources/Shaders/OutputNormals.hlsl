#include "Common.hlsli"

#define RootSig "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
				"CBV(b0, visibility=SHADER_VISIBILITY_VERTEX), " \
				"DescriptorTable(SRV(t0, numDescriptors = 1), visibility=SHADER_VISIBILITY_PIXEL), " \
				"StaticSampler(s0, filter=FILTER_MIN_MAG_MIP_LINEAR, visibility = SHADER_VISIBILITY_PIXEL), "

cbuffer PerObjectData : register(b0)
{
	float4x4 cWorld;
	float4x4 cWorldViewProjection;
}

Texture2D tNormalTexture : register(t0);
SamplerState sSampler : register(s0);

struct VSInput
{
	float3 position : POSITION;
	float2 texCoord : TEXCOORD;
	float3 normal : NORMAL;
	float3 tangent : TANGENT;
	float3 bitangent : TEXCOORD1;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD;
	float3 normal : NORMAL;
	float3 tangent : TANGENT;
	float3 bitangent : TEXCOORD1;
};

float3 CalculateNormal(float3 N, float3 T, float3 BT, float2 tex, bool invertY)
{
	float3x3 normalMatrix = float3x3(T, BT, N);
	float3 sampledNormal = tNormalTexture.Sample(sSampler, tex).rgb;
	sampledNormal.xy = sampledNormal.xy * 2.0f - 1.0f;
	if(invertY)
	{
		sampledNormal.y = -sampledNormal.y;
	}
	sampledNormal = normalize(sampledNormal);
	return mul(sampledNormal, normalMatrix);
}

[RootSignature(RootSig)]
PSInput VSMain(VSInput input)
{
	PSInput result = (PSInput)0;
	result.position = mul(float4(input.position, 1.0f), cWorldViewProjection);
	result.texCoord = input.texCoord;
	result.normal = mul(input.normal, (float3x3)cWorld).xyz;
	result.tangent = mul(input.tangent, (float3x3)cWorld).xyz;
	result.bitangent = mul(input.bitangent, (float3x3)cWorld).xyz;
	return result;
}

float4 PSMain(PSInput input) : SV_Target0
{
	return float4(CalculateNormal(normalize(input.normal), normalize(input.tangent), normalize(input.bitangent), input.texCoord, false), 0);
}