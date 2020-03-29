#include "Common.hlsli"
#include "Lighting.hlsli"

#define SPLITZ_CULLING 1
#define FORWARD_PLUS 1
#define BLOCK_SIZE 16

#define RootSig "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
				"CBV(b0, visibility=SHADER_VISIBILITY_VERTEX), " \
				"CBV(b1, visibility=SHADER_VISIBILITY_ALL), " \
				"CBV(b2, visibility=SHADER_VISIBILITY_PIXEL), " \
				"DescriptorTable(SRV(t0, numDescriptors = 3), visibility=SHADER_VISIBILITY_PIXEL), " \
				"DescriptorTable(SRV(t3, numDescriptors = 4), visibility=SHADER_VISIBILITY_PIXEL), " \
				"StaticSampler(s0, filter=FILTER_MIN_MAG_MIP_LINEAR, visibility = SHADER_VISIBILITY_PIXEL), " \
				"StaticSampler(s1, filter=FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, visibility = SHADER_VISIBILITY_PIXEL, comparisonFunc=COMPARISON_GREATER), " \

cbuffer PerObjectData : register(b0)
{
	float4x4 cWorld;
	float4x4 cWorldViewProjection;
}

cbuffer PerFrameData : register(b1)
{
	float4x4 cViewInverse;
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
	float3 worldPosition : TEXCOORD3;
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

[RootSignature(RootSig)]
PSInput VSMain(VSInput input)
{
	PSInput result;
	result.position = mul(float4(input.position, 1.0f), cWorldViewProjection);
	result.texCoord = input.texCoord;
	result.normal = normalize(mul(input.normal, (float3x3)cWorld));
	result.tangent = normalize(mul(input.tangent, (float3x3)cWorld));
	result.bitangent = normalize(mul(input.bitangent, (float3x3)cWorld));
	result.worldPosition = mul(float4(input.position, 1.0f), cWorld).xyz;
	return result;
}

float3 CalculateNormal(float3 normal, float3 tangent, float3 bitangent, float2 texCoord, bool invertY)
{
	float3x3 normalMatrix = float3x3(tangent, bitangent, normal);
	float3 sampledNormal = tNormalTexture.Sample(sDiffuseSampler, texCoord).rgb;
	sampledNormal.xy = sampledNormal.xy * 2.0f - 1.0f;
	if(invertY)
	{
		sampledNormal.y = -sampledNormal.y;
	}
	sampledNormal = normalize(sampledNormal);
	return mul(sampledNormal, normalMatrix);
}

LightResult DoLight(float4 pos, float3 worldPos, float3 N, float3 V, float3 diffuseColor, float3 specularColor, float roughness)
{
#if FORWARD_PLUS
	uint2 tileIndex = uint2(floor(pos.xy / BLOCK_SIZE));
	uint startOffset = tLightGrid[tileIndex].x;
	uint lightCount = tLightGrid[tileIndex].y;
#else
	uint lightCount = cLightCount;
#endif
	LightResult totalResult = (LightResult)0;

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
		if(light.Type != 0 && distance(worldPos, light.Position) > light.Range)
		{
			continue;
		}
#endif
		LightResult result = DoLight(light, specularColor, diffuseColor, roughness, worldPos, N, V);

		totalResult.Diffuse += result.Diffuse * light.Color.rgb * light.Color.w;
		totalResult.Specular += result.Specular * light.Color.rgb * light.Color.w;
	}

	return totalResult;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	float4 baseColor = tDiffuseTexture.Sample(sDiffuseSampler, input.texCoord);
	float3 specular = 1;
	float metalness = 0;
	float r = lerp(0.3f, 1.0f, 1 - tSpecularTexture.Sample(sDiffuseSampler, input.texCoord).r);

	float3 diffuseColor = baseColor.rgb * (1 - metalness);
	float3 specularColor = ComputeF0(specular.r, baseColor.rgb, metalness);

	float3 N = CalculateNormal(normalize(input.normal), normalize(input.tangent), normalize(input.bitangent), input.texCoord, false);
	float3 V = normalize(cViewInverse[3].xyz - input.worldPosition);

	LightResult lighting = DoLight(input.position, input.worldPosition, N, V, diffuseColor, specularColor, r);
	
	float3 color = lighting.Diffuse + lighting.Specular; 
	color += ApplyAmbientLight(diffuseColor, 1, 0.01f);

	return float4(color, baseColor.a);
}