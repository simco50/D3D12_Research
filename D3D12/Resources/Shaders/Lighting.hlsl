#ifndef H_LIGHTING
#define H_LIGHTING

#include "Common.hlsl"

cbuffer LightData : register(b2)
{
	float4x4 cLightViewProjections[MAX_SHADOW_CASTERS];
	float4 cShadowMapOffsets[MAX_SHADOW_CASTERS];
}

struct LightResult
{
	float4 Diffuse;
	float4 Specular;
};

float GetSpecularBlinnPhong(float3 viewDirection, float3 normal, float3 lightVector, float shininess)
{
	float3 hv = normalize(lightVector - viewDirection);
	float specularStrength = dot(hv, normal);
	return pow(saturate(specularStrength), shininess);
}

float GetSpecularPhong(float3 viewDirection, float3 normal, float3 lightVector, float shininess)
{
	float3 reflectedLight = reflect(-lightVector, normal);
	float specularStrength = max(0, dot(reflectedLight, -viewDirection));
	return pow((specularStrength), shininess);
}

float4 DoDiffuse(Light light, float3 normal, float3 lightVector)
{
	return float4(light.Color.rgb * max(dot(normal, lightVector), 0), 1);
}

float4 DoSpecular(Light light, float3 normal, float3 lightVector, float3 viewDirection)
{
	return float4(light.Color.rgb * GetSpecularPhong(viewDirection, normal, lightVector, 20.0f), 1);
}

float DoAttenuation(Light light, float d)
{
    return 1.0f - smoothstep(light.Range * light.Attenuation, light.Range, d);
}

#ifdef SHADOW
Texture2D tShadowMapTexture : register(t3);
SamplerComparisonState sShadowMapSampler : register(s2);

float DoShadow(float3 worldPosition, int shadowMapIndex)
{
	float4x4 lightViewProjection = cLightViewProjections[shadowMapIndex];

	float4 lightPos = mul(float4(worldPosition, 1), lightViewProjection);
	lightPos.xyz /= lightPos.w;
	lightPos.x = lightPos.x / 2.0f + 0.5f;
	lightPos.y = lightPos.y / -2.0f + 0.5f;

	float shadowFactor = 0;
	int hKernel = (PCF_KERNEL_SIZE - 1) / 2;

	float2 shadowMapStart = cShadowMapOffsets[shadowMapIndex].xy;
	float normalizedShadowMapSize = cShadowMapOffsets[shadowMapIndex].z;

	for(int x = -hKernel; x <= hKernel; ++x)
	{
		for(int y = -hKernel; y <= hKernel; ++y)
		{
			float2 texCoord = shadowMapStart + lightPos.xy * normalizedShadowMapSize + float2(SHADOWMAP_DX * x, SHADOWMAP_DX * y); 
			shadowFactor += tShadowMapTexture.SampleCmpLevelZero(sShadowMapSampler, texCoord, lightPos.z);
		}
	}
	return shadowFactor / (PCF_KERNEL_SIZE * PCF_KERNEL_SIZE);
}

#endif

LightResult DoPointLight(Light light, float3 worldPosition, float3 normal, float3 viewDirection)
{
	LightResult result;
	float3 L = light.Position - worldPosition;
	float d = length(L);
	L = L / d;

	float attenuation = DoAttenuation(light, d);
	result.Diffuse = light.Color.w * attenuation * DoDiffuse(light, normal, L);
	result.Specular = light.Color.w * attenuation * DoSpecular(light, normal, L, viewDirection);

#ifdef SHADOW
	if(light.ShadowIndex != -1)
	{
		float3 vLight = normalize(worldPosition - light.Position);
		float faceIndex = GetCubeFaceIndex(vLight);

		float shadowFactor = DoShadow(worldPosition, light.ShadowIndex + faceIndex);
		result.Diffuse *= shadowFactor;
		result.Specular *= shadowFactor;
	}
#endif
	return result;
}

LightResult DoDirectionalLight(Light light, float3 worldPosition, float3 normal, float3 viewDirection)
{
	LightResult result;
	result.Diffuse = light.Color.w * DoDiffuse(light, normal, -light.Direction);
	result.Specular = light.Color.w * DoSpecular(light, normal, -light.Direction, viewDirection);

#ifdef SHADOW
	if(light.ShadowIndex != -1)
	{
		float shadowFactor = DoShadow(worldPosition, light.ShadowIndex);
		result.Diffuse *= shadowFactor;
		result.Specular *= shadowFactor;
	}
#endif

	return result;
}

LightResult DoSpotLight(Light light, float3 worldPosition, float3 normal, float3 viewDirection)
{
	LightResult result = (LightResult)0;
	float3 L = light.Position - worldPosition;
	float d = length(L);
	L = L / d;

	float minCos = cos(radians(light.SpotLightAngle));
	float maxCos = lerp(minCos, 1.0f, 1 - light.Attenuation);
	float cosAngle = dot(-L, light.Direction);
	float spotIntensity = smoothstep(minCos, maxCos, cosAngle);

	float attenuation = DoAttenuation(light, d);

	result.Diffuse = light.Color.w * attenuation * spotIntensity * DoDiffuse(light, normal, L);
	result.Specular = light.Color.w * attenuation * spotIntensity * DoSpecular(light, normal, L, viewDirection);

#ifdef SHADOW
	if(light.ShadowIndex != -1)
	{
		float shadowFactor = DoShadow(worldPosition, light.ShadowIndex);
		result.Diffuse *= shadowFactor;
		result.Specular *= shadowFactor;
	}
#endif

	return result;
}

#endif