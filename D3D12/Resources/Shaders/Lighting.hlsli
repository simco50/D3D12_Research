#pragma once

#include "Common.hlsli"
#include "ShadingModels.hlsli"
#include "SkyCommon.hlsli"

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
	float3 normal = UnpackBC5Normal(sampledNormal.xy);
	normal.xy = sampledNormal.xy * 2.0f - 1.0f;
	normal.y = -normal.y;
	normal = normalize(normal);
	return mul(normal, TBN);
}

float LightTextureMask(Light light, float3 worldPosition)
{
	float mask = 1.0f;
	if(light.MaskTexture != INVALID_HANDLE)
	{
		uint matrixIndex = light.MatrixIndex;
		float4 lightPos = mul(float4(worldPosition, 1), cView.LightMatrices[matrixIndex]);
		lightPos.xyz /= lightPos.w;
		lightPos.xy = (lightPos.xy + 1) / 2;
		mask = SampleLevel2D(light.MaskTexture, sLinearClamp, lightPos.xy, 0).r;
	}
	return mask;
}

uint GetShadowMapIndex(Light light, float3 worldPosition, float viewDepth, float dither)
{
	if(light.IsDirectional)
	{
		float4 splits = viewDepth > cView.CascadeDepths;
		float4 cascades = cView.CascadeDepths > 0;
		int cascadeIndex = min(dot(splits, cascades), cView.NumCascades - 1);

		const float cascadeFadeTheshold = 0.1f;
		float nextSplit = cView.CascadeDepths[cascadeIndex];
		float splitRange = cascadeIndex == 0 ? nextSplit : nextSplit - cView.CascadeDepths[cascadeIndex - 1];
		float fadeFactor = (nextSplit - viewDepth) / splitRange;
		if(fadeFactor <= cascadeFadeTheshold && cascadeIndex < cView.NumCascades - 1)
		{
			float lerpAmount = smoothstep(0.0f, cascadeFadeTheshold, fadeFactor);
			if(lerpAmount < dither)
			{
				cascadeIndex++;
			}
		}
		return cascadeIndex;
	}
	else if(light.IsPoint)
	{
		return GetCubeFaceIndex(worldPosition - light.Position);
	}
	return 0;
}

float Shadow3x3PCF(float3 wPos, int lightMatrix, int shadowMapIndex, float invShadowSize)
{
	float4x4 lightViewProjection = cView.LightMatrices[lightMatrix];
	float4 lightPos = mul(float4(wPos, 1), lightViewProjection);
	lightPos.xyz /= lightPos.w;
	lightPos.x = lightPos.x / 2.0f + 0.5f;
	lightPos.y = lightPos.y / -2.0f + 0.5f;
	float2 uv = lightPos.xy;
	Texture2D shadowTexture = ResourceDescriptorHeap[NonUniformResourceIndex(shadowMapIndex)];

	const float dilation = 2.0f;
	float d1 = dilation * invShadowSize * 0.125f;
	float d2 = dilation * invShadowSize * 0.875f;
	float d3 = dilation * invShadowSize * 0.625f;
	float d4 = dilation * invShadowSize * 0.375f;
	float result = (
		2.0f * shadowTexture.SampleCmpLevelZero(sLinearClampComparisonGreater, uv, lightPos.z) +
		shadowTexture.SampleCmpLevelZero(sLinearClampComparisonGreater, uv + float2(-d2,  d1), lightPos.z) +
		shadowTexture.SampleCmpLevelZero(sLinearClampComparisonGreater, uv + float2(-d1, -d2), lightPos.z) +
		shadowTexture.SampleCmpLevelZero(sLinearClampComparisonGreater, uv + float2( d2, -d1), lightPos.z) +
		shadowTexture.SampleCmpLevelZero(sLinearClampComparisonGreater, uv + float2( d1,  d2), lightPos.z) +
		shadowTexture.SampleCmpLevelZero(sLinearClampComparisonGreater, uv + float2(-d4,  d3), lightPos.z) +
		shadowTexture.SampleCmpLevelZero(sLinearClampComparisonGreater, uv + float2(-d3, -d4), lightPos.z) +
		shadowTexture.SampleCmpLevelZero(sLinearClampComparisonGreater, uv + float2( d4, -d3), lightPos.z) +
		shadowTexture.SampleCmpLevelZero(sLinearClampComparisonGreater, uv + float2( d3,  d4), lightPos.z)
		) / 10.0f;
	return result * result;
}

float ShadowNoPCF(float3 wPos, int lightMatrix, int shadowMapIndex, float invShadowSize)
{
	float4x4 lightViewProjection = cView.LightMatrices[lightMatrix];
	float4 lightPos = mul(float4(wPos, 1), lightViewProjection);
	lightPos.xyz /= lightPos.w;
	lightPos.x = lightPos.x / 2.0f + 0.5f;
	lightPos.y = lightPos.y / -2.0f + 0.5f;
	float2 uv = lightPos.xy;
	Texture2D shadowTexture = ResourceDescriptorHeap[NonUniformResourceIndex(shadowMapIndex)];
	return shadowTexture.SampleCmpLevelZero(sLinearClampComparisonGreater, uv, lightPos.z);
}

float GetAttenuation(Light light, float3 worldPosition, out float3 L)
{
	float attenuation = 1.0f;
	L = -light.Direction;
	if(light.IsPoint || light.IsSpot)
	{
		L = light.Position - worldPosition;
		attenuation *= RadialAttenuation(L, light.Range);
		if(light.IsSpot)
		{
			attenuation *= DirectionalAttenuation(L, light.Direction, light.SpotlightAngles.y, light.SpotlightAngles.x);
		}

		float distSq = dot(L, L);
		L *= rsqrt(distSq);

	}

	if(attenuation > 0.0f && light.CastShadows)
	{
		attenuation *= LightTextureMask(light, worldPosition);
	}

	return attenuation;
}

float ScreenSpaceShadows(float3 worldPosition, float3 lightDirection, Texture2D<float> depthTexture, int stepCount, float rayLength, float ditherOffset)
{
	float4 rayStartPS = mul(float4(worldPosition, 1), cView.ViewProjection);
	float4 rayDirPS = mul(float4(-lightDirection * rayLength, 0), cView.ViewProjection);
	float4 rayEndPS = rayStartPS + rayDirPS;
	rayStartPS.xyz /= rayStartPS.w;
	rayEndPS.xyz /= rayEndPS.w;
	float3 rayStep = rayEndPS.xyz - rayStartPS.xyz;
	float stepSize = 1.0f / stepCount;

	float4 rayDepthClip = rayStartPS + mul(float4(0, 0, rayLength, 0), cView.Projection);
	rayDepthClip.xyz /= rayDepthClip.w;
	float tolerance = abs(rayDepthClip.z - rayStartPS.z) * stepSize * 2;

	float occlusion = 0.0f;
	float hitStep = -1.0f;

	float n = stepSize * ditherOffset + stepSize;

	[unroll]
	for(uint i = 0; i < stepCount; ++i)
	{
		float3 rayPos = rayStartPS.xyz + n * rayStep;
		float depth = depthTexture.SampleLevel(sLinearClamp, rayPos.xy * float2(0.5f, -0.5f) + float2(0.5f, 0.5f), 0).r;
		float diff = rayPos.z - depth;

		bool hit = abs(diff + tolerance) < tolerance;
		hitStep = hit && hitStep < 0.0f ? n : hitStep;
		n += stepSize;
	}
	if(hitStep > 0.0f)
	{
		float2 hitUV = rayStartPS.xy + n * rayStep.xy;
		hitUV = hitUV * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);
		occlusion = ScreenFade(hitUV);
	}
	return 1.0f - occlusion;
}

float3 ScreenSpaceReflections(float3 worldPosition, float3 N, float3 V, float R, Texture2D<float> depthTexture, Texture2D<float4> previousSceneColor, float dither, inout float ssrWeight)
{
	float3 ssr = 0;
	const float roughnessThreshold = 0.7f;
	bool ssrEnabled = R < roughnessThreshold;
	if(ssrEnabled)
	{
		float reflectionThreshold = 0.0f;
		float3 reflectionWs = normalize(reflect(-V, N));
		if (dot(V, reflectionWs) <= reflectionThreshold)
		{
			float jitter = dither - 1.0f;
			uint maxSteps = cView.SsrSamples;

			float3 rayStartVS = mul(float4(worldPosition, 1), cView.View).xyz;
			float linearDepth = rayStartVS.z;
			float3 reflectionVs = mul(reflectionWs, (float3x3)cView.View);
			float3 rayEndVS = rayStartVS + (reflectionVs * linearDepth);

			float3 rayStart = ViewToWindow(rayStartVS, cView.Projection);
			float3 rayEnd = ViewToWindow(rayEndVS, cView.Projection);

			float3 rayStep = ((rayEnd - rayStart) / float(maxSteps));
			rayStep = rayStep / length(rayEnd.xy - rayStart.xy);
			float3 rayPos = rayStart + (rayStep * jitter);
			float zThickness = abs(rayStep.z);

			uint hitIndex = 0;
			float3 bestHit = rayPos;
			float prevSceneZ = rayStart.z;
			for (uint currStep = 0; currStep < maxSteps; currStep += 4)
			{
				uint4 step = float4(1, 2, 3, 4) + currStep;
				float4 sceneZ = float4(
					depthTexture.SampleLevel(sLinearClamp, rayPos.xy + rayStep.xy * step.x, 0).x,
					depthTexture.SampleLevel(sLinearClamp, rayPos.xy + rayStep.xy * step.y, 0).x,
					depthTexture.SampleLevel(sLinearClamp, rayPos.xy + rayStep.xy * step.z, 0).x,
					depthTexture.SampleLevel(sLinearClamp, rayPos.xy + rayStep.xy * step.w, 0).x
				);
				float4 currentPosition = rayPos.z + rayStep.z * step;
				uint4 zTest = abs(sceneZ - currentPosition - zThickness) < zThickness;
				uint zMask = (((zTest.x << 0) | (zTest.y << 1)) | (zTest.z << 2)) | (zTest.w << 3);
				if(zMask > 0)
				{
					uint firstHit = firstbitlow(zMask);
					if(firstHit > 0)
					{
						prevSceneZ = sceneZ[firstHit - 1];
					}
					bestHit = rayPos + (rayStep * float(currStep + firstHit + 1));
					float zAfter = sceneZ[firstHit] - bestHit.z;
					float zBefore = (prevSceneZ - bestHit.z) + rayStep.z;
					float weight = saturate(zAfter / (zAfter - zBefore));
					float3 prevRayPos = bestHit - rayStep;
					bestHit = (prevRayPos * weight) + (bestHit * (1.0f - weight));
					hitIndex = currStep + firstHit;
					break;
				}
				prevSceneZ = sceneZ.w;
			}

			float4 hitColor = 0;
			if (hitIndex > 0)
			{
				float4 UV = float4(bestHit.xy, 0, 1);
				UV = mul(UV, cView.ReprojectionMatrix);
				float2 distanceFromCenter = (float2(UV.x, UV.y) * 2.0f) - float2(1.0f, 1.0f);
				float edgeAttenuation = saturate((1.0 - ((float)hitIndex / maxSteps)) * 4.0f);
				edgeAttenuation *= smoothstep(0.0f, 0.5f, saturate(1.0 - dot(distanceFromCenter, distanceFromCenter)));
				float3 reflectionResult = previousSceneColor.SampleLevel(sLinearClamp, UV.xy, 0).xyz;
				hitColor = float4(reflectionResult, edgeAttenuation);
			}
			float roughnessMask = saturate(1.0f - (R / roughnessThreshold));
			ssrWeight = (hitColor.w * roughnessMask);
			ssr = saturate(hitColor.xyz * ssrWeight);
		}
	}
	return ssr;
}

LightResult DoLight(Light light, float3 specularColor, float3 diffuseColor, float R, float3 N, float3 V, float3 worldPosition, float linearDepth, float dither)
{
	LightResult result = (LightResult)0;

	float3 L;
	float attenuation = GetAttenuation(light, worldPosition, L);
	if(attenuation <= 0.0f)
		return result;

	if(light.CastShadows)
	{
		uint shadowIndex = GetShadowMapIndex(light, worldPosition, linearDepth, dither);

#define VISUALIZE_CASCADES 0
#if VISUALIZE_CASCADES
		if(light.IsDirectional)
		{
			float4x4 lightViewProjection = cView.LightMatrices[light.MatrixIndex];
			float4 lightPos = mul(float4(worldPosition, 1), lightViewProjection);
			lightPos.xyz /= lightPos.w;
			lightPos.x = lightPos.x / 2.0f + 0.5f;
			lightPos.y = lightPos.y / -2.0f + 0.5f;
			float2 uv = lightPos.xy;
			float strength = 0.1f;

			if(any(uv < 0) || any(uv) > 1)
			{
				float modulate = cos((float)cView.FrameIndex / 30) * 0.5f + 0.5f;
				strength = saturate(strength + modulate * 0.2f);
			}
			static float3 COLORS[] = {
				float3(1,0,0),
				float3(0,1,0),
				float3(0,0,1),
				float3(1,0,1),
			};

			result.Diffuse += strength * COLORS[shadowIndex];
			return result;
		}
#endif

		attenuation *= Shadow3x3PCF(worldPosition, light.MatrixIndex + shadowIndex, light.ShadowMapIndex + shadowIndex, light.InvShadowSize);
		if(attenuation <= 0)
			return result;
	}

	result = DefaultLitBxDF(specularColor, R, diffuseColor, N, V, L, attenuation);

	float3 color = light.GetColor();
	result.Diffuse *= color * light.Intensity;
	result.Specular *= color * light.Intensity;

	return result;
}
