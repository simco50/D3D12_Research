#include "Common.hlsl"
#include "Constants.hlsl"
#include "Lighting.hlsl"

cbuffer PerObjectData : register(b0)
{
	float4x4 cWorld;
	float4x4 cWorldViewProjection;
}

cbuffer PerFrameData : register(b1)
{
	float4x4 cLightViewProjection;
	float4x4 cViewInverse;
}

cbuffer LightData : register(b2)
{
    Light cLights[LIGHT_COUNT];
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
	float4 lpos : TEXCOORD2;
	float4 wpos : TEXCOORD3;
};

Texture2D tDiffuseTexture : register(t0);
SamplerState sDiffuseSampler : register(s0);

Texture2D tNormalTexture : register(t1);
SamplerState sNormalSampler : register(s1);

Texture2D tSpecularTexture : register(t2);

Texture2D tShadowMapTexture : register(t3);
SamplerComparisonState sShadowMapSampler : register(s2);

#ifdef FORWARD_PLUS
Texture2D<uint2> tLightGrid : register(t4);
StructuredBuffer<uint> tLightIndexList : register(t5);
#endif

LightResult DoLight(float4 position, float3 worldPosition, float3 normal, float3 viewDirection, float shadowFactor)
{
#if FORWARD_PLUS
	uint2 tileIndex = uint2(floor(position.xy / BLOCK_SIZE));
	uint startOffset = tLightGrid[tileIndex].x;
	uint lightCount = tLightGrid[tileIndex].y;
#else
	uint lightCount = LIGHT_COUNT;
#endif
	LightResult totalResult = (LightResult)0;

#if DEBUG_VISUALIZE
	totalResult.Diffuse = (float)max(lightCount, 0) / 50.0f;
	return totalResult;
#endif

	for(uint i = 0; i < lightCount; ++i)
	{
#if FORWARD_PLUS
		uint lightIndex = tLightIndexList[startOffset + i];
		Light light = cLights[lightIndex];
#else
		uint lightIndex = i;
		Light light = cLights[i];
		if(light.Enabled == 0)
		{
			continue;
		}
		if(light.Type != 0 && distance(worldPosition, light.Position) > light.Range)
		{
			continue;
		}
#endif
		LightResult result = (LightResult)0;

		switch(light.Type)
		{
		case LIGHT_DIRECTIONAL:
			result = DoDirectionalLight(light, normal, viewDirection);
			break;
		case LIGHT_POINT:
			result = DoPointLight(light, worldPosition, normal, viewDirection);
			break;
		case LIGHT_SPOT:
			result = DoSpotLight(light, worldPosition, normal, viewDirection);
			break;
		default:
			//Unsupported light type
			result.Diffuse = float4(1, 0, 1, 1);
			result.Specular = float4(0, 0, 0, 1);
			break;
		}

		if(lightIndex == 0)
		{
			result.Specular = shadowFactor > 0 ? result.Specular : float4(0, 0, 0, 0);
			result.Diffuse *= shadowFactor;
		}

		totalResult.Diffuse += result.Diffuse;
		totalResult.Specular += result.Specular;
	}

	return totalResult;
}

float3 CalculateNormal(float3 normal, float3 tangent, float3 bitangent, float2 texCoord, bool invertY)
{
	float3x3 normalMatrix = float3x3(tangent, bitangent, normal);
	float3 sampledNormal = tNormalTexture.Sample(sNormalSampler, texCoord).rgb;
	sampledNormal.xy = sampledNormal.xy * 2.0f - 1.0f;
	if(invertY)
	{
		sampledNormal.y = -sampledNormal.y;
	}
	sampledNormal = normalize(sampledNormal);
	return mul(sampledNormal, normalMatrix);
}

PSInput VSMain(VSInput input)
{
	PSInput result;
	
	result.position = mul(float4(input.position, 1.0f), cWorldViewProjection);
	result.texCoord = input.texCoord;
	result.normal = normalize(mul(input.normal, (float3x3)cWorld));
	result.tangent = normalize(mul(input.tangent, (float3x3)cWorld));
	result.bitangent = normalize(mul(input.bitangent, (float3x3)cWorld));
	result.lpos = mul(float4(input.position, 1.0f), mul(cWorld, cLightViewProjection));
	result.wpos = mul(float4(input.position, 1.0f), cWorld);
	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	float4 diffuseSample = tDiffuseTexture.Sample(sDiffuseSampler, input.texCoord);
	if(diffuseSample.a <= 0.01f)
	{
		discard;
	}

	float3 viewDirection = normalize(input.wpos.xyz - cViewInverse[3].xyz);
	float3 normal = CalculateNormal(normalize(input.normal), normalize(input.tangent), normalize(input.bitangent), input.texCoord, true);

	input.lpos.xyz /= input.lpos.w;
	input.lpos.x = input.lpos.x / 2.0f + 0.5f;
	input.lpos.y = input.lpos.y / -2.0f + 0.5f;
	input.lpos.z -= 0.001f;

	int width, height;
	tShadowMapTexture.GetDimensions(width, height);
	float dx = 1.0f / width;
	float dy = 1.0f / height;

    float shadowFactor = 0;
	int kernelSize = 3;
	int hKernel = (kernelSize - 1) / 2;
	for(int x = -hKernel; x <= hKernel; ++x)
	{
		for(int y = -hKernel; y <= hKernel; ++y)
		{
    		shadowFactor += tShadowMapTexture.SampleCmpLevelZero(sShadowMapSampler, input.lpos.xy + float2(dx * x, dy * y), input.lpos.z);
		}
	}
	shadowFactor /= kernelSize * kernelSize;

    LightResult lightResults = DoLight(input.position, input.wpos.xyz, input.normal, viewDirection, shadowFactor);
    float4 specularSample = tSpecularTexture.Sample(sDiffuseSampler, input.texCoord);
    lightResults.Specular *= specularSample;
#if !DEBUG_VISUALIZE
   	lightResults.Diffuse *= diffuseSample;
#endif
	return saturate(lightResults.Diffuse + lightResults.Specular);
}