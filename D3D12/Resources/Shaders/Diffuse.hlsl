#include "Common.hlsl"
#include "Constants.hlsl"
#include "Lighting.hlsl"

#define RootSig "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
				"CBV(b0, visibility=SHADER_VISIBILITY_VERTEX), " \
				"CBV(b1, visibility=SHADER_VISIBILITY_ALL), " \
				"CBV(b2, visibility=SHADER_VISIBILITY_PIXEL), " \
				"DescriptorTable(SRV(t0, numDescriptors = 3), visibility=SHADER_VISIBILITY_PIXEL), " \
				"DescriptorTable(SRV(t3, numDescriptors = 4), visibility=SHADER_VISIBILITY_PIXEL), " \
				"StaticSampler(s0, filter=FILTER_MIN_MAG_MIP_LINEAR, visibility = SHADER_VISIBILITY_PIXEL), " \
				"StaticSampler(s2, filter=FILTER_MIN_MAG_MIP_POINT, visibility = SHADER_VISIBILITY_PIXEL, comparisonFunc = COMPARISON_GREATER_EQUAL)"

cbuffer PerObjectData : register(b0)
{
	float4x4 cWorld;
	float4x4 cWorldViewProjection;
}

cbuffer PerFrameData : register(b1)
{
	float4x4 cViewInverse;
	uint cLightCount;
}

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
	float4 worldPosition : TEXCOORD3;
};

Texture2D tDiffuseTexture : register(t0);
SamplerState sDiffuseSampler : register(s0);

Texture2D tNormalTexture : register(t1);

Texture2D tSpecularTexture : register(t2);

#ifdef FORWARD_PLUS
Texture2D<uint2> tLightGrid : register(t4);
StructuredBuffer<uint> tLightIndexList : register(t5);
#endif

StructuredBuffer<Light> Lights : register(t6);

LightResult DoLight(float4 pos, float3 wPos, float3 N, float3 V)
{
#if FORWARD_PLUS
	uint2 tileIndex = uint2(floor(pos.xy / BLOCK_SIZE));
	uint startOffset = tLightGrid[tileIndex].x;
	uint lightCount = tLightGrid[tileIndex].y;
#else
	uint lightCount = cLightCount;
#endif
	LightResult totalResult = (LightResult)0;

#if DEBUG_VISUALIZE
	totalResult.Diffuse = (float)max(lightCount, 0) / 100.0f;
	return totalResult;
#endif

	for(uint i = 0; i < lightCount; ++i)
	{
#if FORWARD_PLUS
		uint lightIndex = tLightIndexList[startOffset + i];
		Light light = Lights[lightIndex];
#else
		uint lightIndex = i;
		Light light = Lights[i];
		if(light.Enabled == 0)
		{
			continue;
		}
		if(light.Type != 0 && distance(wPos, light.Position) > light.Range)
		{
			continue;
		}
#endif
		LightResult result = DoLight(light, wPos, N, V);
		totalResult.Diffuse += result.Diffuse;
		totalResult.Specular += result.Specular;
	}

	return totalResult;
}

float3 CalculateNormal(float3 N, float3 T, float3 BT, float2 tex, bool invertY)
{
	float3x3 normalMatrix = float3x3(T, BT, N);
	float3 sampledNormal = tNormalTexture.Sample(sDiffuseSampler, tex).rgb;
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
	PSInput result;
	result.position = mul(float4(input.position, 1.0f), cWorldViewProjection);
	result.texCoord = input.texCoord;
	result.normal = normalize(mul(input.normal, (float3x3)cWorld));
	result.tangent = normalize(mul(input.tangent, (float3x3)cWorld));
	result.bitangent = normalize(mul(input.bitangent, (float3x3)cWorld));
	result.worldPosition = mul(float4(input.position, 1.0f), cWorld);
	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	float4 diffuseSample = tDiffuseTexture.Sample(sDiffuseSampler, input.texCoord);

	float3 V = normalize(input.worldPosition.xyz - cViewInverse[3].xyz);
	float3 N = CalculateNormal(normalize(input.normal), normalize(input.tangent), normalize(input.bitangent), input.texCoord, true);

    LightResult lightResults = DoLight(input.position, input.worldPosition.xyz, N, V);
    float4 specularSample = tSpecularTexture.Sample(sDiffuseSampler, input.texCoord);
    lightResults.Specular *= specularSample;
#if !DEBUG_VISUALIZE
   	lightResults.Diffuse *= diffuseSample;
#endif

	float4 color = saturate(lightResults.Diffuse + lightResults.Specular);
	color.a = diffuseSample.a;

	return color;
}