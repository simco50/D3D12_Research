#pragma once

#include "CommonBindings.hlsli"
#include "ShadingModels.hlsli"

#define SUPPORT_BC5 1

struct BrdfData
{
	float3 Diffuse;
	float3 Specular;
	float Roughness;
};

BrdfData GetBrdfData(MaterialProperties material)
{
	BrdfData data;
	data.Diffuse = ComputeDiffuseColor(material.BaseColor, material.Metalness);
	data.Specular = ComputeF0(material.Specular, material.BaseColor, material.Metalness);
	data.Roughness = material.Roughness;
	return data;
}

// Angle >= Umbra -> 0
// Angle < Penumbra -> 1
//Gradient between Umbra and Penumbra
float DirectionalAttenuation(float3 L, float3 direction, float cosUmbra, float cosPenumbra)
{
	float cosAngle = dot(-normalize(L), direction);
	float falloff = saturate((cosAngle - cosUmbra) / (cosPenumbra - cosUmbra));
	return falloff * falloff;
}

//Distance between rays is proportional to distance squared
//Extra windowing function to make light radius finite
//https://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf
float RadialAttenuation(float3 L, float range)
{
	float distSq = dot(L, L);
	float distanceAttenuation = 1 / (distSq + 1);
	float windowing = Square(saturate(1 - Square(distSq * Square(rcp(range)))));
	return distanceAttenuation * windowing;
}

// Unpacks a 2 channel BC5 normal to xyz
float3 UnpackBC5Normal(float2 packedNormal)
{
	return float3(packedNormal, sqrt(1 - saturate(dot(packedNormal.xy, packedNormal.xy))));
}

float3 TangentSpaceNormalMapping(float3 sampledNormal, float3x3 TBN)
{
	float3 normal = sampledNormal;
#if SUPPORT_BC5
	normal = UnpackBC5Normal(sampledNormal.xy);
#endif
	normal.xy = sampledNormal.xy * 2.0f - 1.0f;
	normal.y = -normal.y;
	normal = normalize(normal);
	return mul(normal, TBN);
}

float LightTextureMask(Light light, int shadowMapIndex, float3 worldPosition)
{
	float mask = 1.0f;
	if(light.LightTexture != INVALID_HANDLE)
	{
		float4 lightPos = mul(float4(worldPosition, 1), cView.LightViewProjections[shadowMapIndex]);
		lightPos.xyz /= lightPos.w;
		lightPos.xy = (lightPos.xy + 1) / 2;
		mask = SampleLevel2D(light.LightTexture, sLinearClamp, lightPos.xy, 0).r;
	}
	return mask;
}

float Shadow3x3PCF(float3 wPos, int shadowMapIndex, float invShadowSize)
{
	float4x4 lightViewProjection = cView.LightViewProjections[shadowMapIndex];
	float4 lightPos = mul(float4(wPos, 1), lightViewProjection);
	lightPos.xyz /= lightPos.w;
	lightPos.x = lightPos.x / 2.0f + 0.5f;
	lightPos.y = lightPos.y / -2.0f + 0.5f;

	float2 uv = lightPos.xy;

	Texture2D shadowTexture = tTexture2DTable[NonUniformResourceIndex(cView.ShadowMapOffset + shadowMapIndex)];

	const float Dilation = 2.0f;
	float d1 = Dilation * invShadowSize * 0.125f;
	float d2 = Dilation * invShadowSize * 0.875f;
	float d3 = Dilation * invShadowSize * 0.625f;
	float d4 = Dilation * invShadowSize * 0.375f;
	float result = (
		2.0f * shadowTexture.SampleCmpLevelZero(sDepthComparison, uv, lightPos.z) +
		shadowTexture.SampleCmpLevelZero(sDepthComparison, uv + float2(-d2,  d1), lightPos.z) +
		shadowTexture.SampleCmpLevelZero(sDepthComparison, uv + float2(-d1, -d2), lightPos.z) +
		shadowTexture.SampleCmpLevelZero(sDepthComparison, uv + float2( d2, -d1), lightPos.z) +
		shadowTexture.SampleCmpLevelZero(sDepthComparison, uv + float2( d1,  d2), lightPos.z) +
		shadowTexture.SampleCmpLevelZero(sDepthComparison, uv + float2(-d4,  d3), lightPos.z) +
		shadowTexture.SampleCmpLevelZero(sDepthComparison, uv + float2(-d3, -d4), lightPos.z) +
		shadowTexture.SampleCmpLevelZero(sDepthComparison, uv + float2( d4, -d3), lightPos.z) +
		shadowTexture.SampleCmpLevelZero(sDepthComparison, uv + float2( d3,  d4), lightPos.z)
		) / 10.0f;
	return result * result;
}

float ShadowNoPCF(float3 wPos, int shadowMapIndex, float invShadowSize)
{
	float4x4 lightViewProjection = cView.LightViewProjections[shadowMapIndex];
	float4 lightPos = mul(float4(wPos, 1), lightViewProjection);
	lightPos.xyz /= lightPos.w;
	lightPos.x = lightPos.x / 2.0f + 0.5f;
	lightPos.y = lightPos.y / -2.0f + 0.5f;
	float2 uv = lightPos.xy;
	Texture2D shadowTexture = tTexture2DTable[NonUniformResourceIndex(cView.ShadowMapOffset + shadowMapIndex)];
	return shadowTexture.SampleCmpLevelZero(sDepthComparison, uv, lightPos.z);
}

float GetAttenuation(Light light, float3 wPos)
{
	float attentuation = 1.0f;

	if(light.PointAttenuation())
	{
		float3 L = light.Position - wPos;
		attentuation *= RadialAttenuation(L, light.Range);
		if(light.DirectionalAttenuation())
		{
			attentuation *= DirectionalAttenuation(L, light.Direction, light.SpotlightAngles.y, light.SpotlightAngles.x);
		}
	}

	return attentuation;
}

float3 ApplyAmbientLight(float3 diffuse, float ao, float3 lightColor)
{
	return ao * diffuse * lightColor;
}

uint GetShadowIndex(Light light, float4 pos, float3 wPos)
{
	int shadowIndex = light.ShadowIndex;
	if(light.IsDirectional)
	{
		float4 splits = pos.w > cView.CascadeDepths;
		float4 cascades = cView.CascadeDepths > 0;
		int cascadeIndex = dot(splits, cascades);

		const float cascadeFadeTheshold = 0.1f;
		float nextSplit = cView.CascadeDepths[cascadeIndex];
		float splitRange = cascadeIndex == 0 ? nextSplit : nextSplit - cView.CascadeDepths[cascadeIndex - 1];
		float fadeFactor = (nextSplit - pos.w) / splitRange;
		if(fadeFactor <= cascadeFadeTheshold && cascadeIndex != cView.NumCascades - 1)
		{
			float lerpAmount = smoothstep(0.0f, cascadeFadeTheshold, fadeFactor);
			float dither = InterleavedGradientNoise(pos.xy);
			if(lerpAmount < dither)
			{
				cascadeIndex++;
			}
		}
		shadowIndex += cascadeIndex;
	}
	else if(light.IsPoint)
	{
		shadowIndex += GetCubeFaceIndex(wPos - light.Position);
	}
	return shadowIndex;
}

LightResult DoLight(Light light, float3 specularColor, float3 diffuseColor, float roughness, float4 pos, float3 wPos, float3 N, float3 V)
{
	LightResult result = (LightResult)0;

	float attenuation = GetAttenuation(light, wPos);
	if(attenuation <= 0)
	{
		return result;
	}

	if(light.ShadowIndex >= 0)
	{
		int shadowIndex = GetShadowIndex(light, pos, wPos);

#define VISUALIZE_CASCADES 0
#if VISUALIZE_CASCADES
		if(light.IsDirectional)
		{
			static float4 COLORS[4] = {
				float4(1,0,0,1),
				float4(0,1,0,1),
				float4(0,0,1,1),
				float4(1,0,1,1),
			};
			result.Diffuse += 0.4f * COLORS[shadowIndex - light.ShadowIndex].xyz;
			result.Specular = 0;
			return result;
		}
#endif

		attenuation *= LightTextureMask(light, shadowIndex, wPos);
		attenuation *= Shadow3x3PCF(wPos, shadowIndex, light.InvShadowSize);
		if(attenuation <= 0)
		{
			return result;
		}
	}

	float3 L = normalize(light.Position - wPos);
	if(light.IsDirectional)
	{
		L = -light.Direction;
	}
	result = DefaultLitBxDF(specularColor, roughness, diffuseColor, N, V, L, attenuation);

	float4 color = light.GetColor();
	result.Diffuse *= color.rgb * light.Intensity;
	result.Specular *= color.rgb * light.Intensity;

	return result;
}
