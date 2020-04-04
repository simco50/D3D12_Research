#include "Common.hlsli"
#include "ShadingModels.hlsli"

#define PCF_KERNEL_SIZE 3
#define SHADOWMAP_DX 0.000244140625f
#define MAX_SHADOW_CASTERS 8

cbuffer LightData : register(b2)
{
	float4x4 cLightViewProjections[MAX_SHADOW_CASTERS];
	float4 cShadowMapOffsets[MAX_SHADOW_CASTERS];
}

float DoAttenuation(Light light, float d)
{
    return 1.0f - smoothstep(light.Range * light.Attenuation, light.Range, d);
}

float3 TangentSpaceNormalMapping(Texture2D normalTexture, SamplerState normalSampler, float3x3 TBN, float2 tex, bool invertY)
{
	float3 sampledNormal = normalTexture.Sample(normalSampler, tex).rgb;
	sampledNormal.xy = sampledNormal.xy * 2.0f - 1.0f;
	if(invertY)
	{
		sampledNormal.y = -sampledNormal.y;
	}
	sampledNormal = normalize(sampledNormal);
	return mul(sampledNormal, TBN);
}

#ifdef SHADOW
Texture2D tShadowMapTexture : register(t3);
SamplerComparisonState sShadowMapSampler : register(s1);

float DoShadow(float3 wPos, int shadowMapIndex)
{
	float4x4 lightViewProjection = cLightViewProjections[shadowMapIndex];
	float4 lightPos = mul(float4(wPos, 1), lightViewProjection);
	lightPos.xyz /= lightPos.w;
	lightPos.x = lightPos.x / 2.0f + 0.5f;
	lightPos.y = lightPos.y / -2.0f + 0.5f;

	float2 shadowMapStart = cShadowMapOffsets[shadowMapIndex].xy;
	float normalizedShadowMapSize = cShadowMapOffsets[shadowMapIndex].z;

	float2 texCoord = shadowMapStart + lightPos.xy * normalizedShadowMapSize; 
	
	const float Dilation = 2.0f;
    float d1 = Dilation * SHADOWMAP_DX * 0.125f;
    float d2 = Dilation * SHADOWMAP_DX * 0.875f;
    float d3 = Dilation * SHADOWMAP_DX * 0.625f;
    float d4 = Dilation * SHADOWMAP_DX * 0.375f;
    float result = (
        2.0f * tShadowMapTexture.SampleCmpLevelZero(sShadowMapSampler, texCoord, lightPos.z) +
        tShadowMapTexture.SampleCmpLevelZero(sShadowMapSampler, texCoord + float2(-d2,  d1), lightPos.z) +
        tShadowMapTexture.SampleCmpLevelZero(sShadowMapSampler, texCoord + float2(-d1, -d2), lightPos.z) +
        tShadowMapTexture.SampleCmpLevelZero(sShadowMapSampler, texCoord + float2( d2, -d1), lightPos.z) +
        tShadowMapTexture.SampleCmpLevelZero(sShadowMapSampler, texCoord + float2( d1,  d2), lightPos.z) +
        tShadowMapTexture.SampleCmpLevelZero(sShadowMapSampler, texCoord + float2(-d4,  d3), lightPos.z) +
        tShadowMapTexture.SampleCmpLevelZero(sShadowMapSampler, texCoord + float2(-d3, -d4), lightPos.z) +
        tShadowMapTexture.SampleCmpLevelZero(sShadowMapSampler, texCoord + float2( d4, -d3), lightPos.z) +
        tShadowMapTexture.SampleCmpLevelZero(sShadowMapSampler, texCoord + float2( d3,  d4), lightPos.z)
        ) / 10.0f;
    return result * result;
}
#endif

float GetAttenuation(Light light, float3 wPos)
{
	float attentuation = 1.0f;

	if(light.Type >= LIGHT_POINT)
	{
		float3 L = light.Position - wPos;
		float d = length(L);
		L = L / d;
		attentuation *= DoAttenuation(light, d);

		if(light.Type >= LIGHT_SPOT)
		{
			float minCos = light.CosSpotLightAngle;
			float maxCos = lerp(minCos, 1.0f, 1 - light.Attenuation);
			float cosAngle = dot(-L, light.Direction);
			float spotFalloff = smoothstep(minCos, maxCos, cosAngle);

			attentuation *= spotFalloff;
		}
	}

	return attentuation;
}

float3 ApplyAmbientLight(float3 diffuse, float ao, float3 lightColor)
{
    return ao * diffuse * lightColor;
}

LightResult DoLight(Light light, float3 specularColor, float3 diffuseColor, float roughness, float3 wPos, float3 N, float3 V)
{
	float attenuation = GetAttenuation(light, wPos);
	float3 L = normalize(light.Position - wPos);
	LightResult result = DefaultLitBxDF(specularColor, roughness, diffuseColor, N, V, L, attenuation);

#ifdef SHADOW
	if(light.ShadowIndex >= 0)
	{
		float s = DoShadow(wPos, light.ShadowIndex);
		result.Diffuse *= s;
		result.Specular *= s;
	}
#endif

	result.Diffuse *= light.Color.rgb * light.Color.w;
	result.Specular *= light.Color.rgb * light.Color.w;

	return result;
}