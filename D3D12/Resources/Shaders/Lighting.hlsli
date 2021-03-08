#include "Common.hlsli"
#include "ShadingModels.hlsli"
#include "CommonBindings.hlsli"

#define SUPPORT_BC5 1
#define MAX_SHADOW_CASTERS 32

struct ShadowData
{
	float4x4 LightViewProjections[MAX_SHADOW_CASTERS];
	float4 CascadeDepths;
	uint NumCascades;
	uint ShadowMapOffset;
};

ConstantBuffer<ShadowData> cShadowData : register(b2);

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

float3 TangentSpaceNormalMapping(float3 sampledNormal, float3x3 TBN, bool invertY)
{
	float3 normal = sampledNormal;
#if SUPPORT_BC5
	normal = UnpackBC5Normal(sampledNormal.xy);
#endif
	normal.xy = sampledNormal.xy * 2.0f - 1.0f;

	if(invertY)
	{
		normal.x = -normal.x;
	}
	normal = normalize(normal);
	return mul(normal, TBN);
}

float DoShadow(float3 wPos, int shadowMapIndex, float invShadowSize)
{
	float4x4 lightViewProjection = cShadowData.LightViewProjections[shadowMapIndex];
	float4 lightPos = mul(float4(wPos, 1), lightViewProjection);
	lightPos.xyz /= lightPos.w;
	lightPos.x = lightPos.x / 2.0f + 0.5f;
	lightPos.y = lightPos.y / -2.0f + 0.5f;

	float2 texCoord = lightPos.xy;

	Texture2D shadowTexture = tTexture2DTable[NonUniformResourceIndex(cShadowData.ShadowMapOffset + shadowMapIndex)];
	
	const float Dilation = 2.0f;
    float d1 = Dilation * invShadowSize * 0.125f;
    float d2 = Dilation * invShadowSize * 0.875f;
    float d3 = Dilation * invShadowSize * 0.625f;
    float d4 = Dilation * invShadowSize * 0.375f;
    float result = (
        2.0f * shadowTexture.SampleCmpLevelZero(sShadowMapSampler, texCoord, lightPos.z) +
        shadowTexture.SampleCmpLevelZero(sShadowMapSampler, texCoord + float2(-d2,  d1), lightPos.z) +
        shadowTexture.SampleCmpLevelZero(sShadowMapSampler, texCoord + float2(-d1, -d2), lightPos.z) +
        shadowTexture.SampleCmpLevelZero(sShadowMapSampler, texCoord + float2( d2, -d1), lightPos.z) +
        shadowTexture.SampleCmpLevelZero(sShadowMapSampler, texCoord + float2( d1,  d2), lightPos.z) +
        shadowTexture.SampleCmpLevelZero(sShadowMapSampler, texCoord + float2(-d4,  d3), lightPos.z) +
        shadowTexture.SampleCmpLevelZero(sShadowMapSampler, texCoord + float2(-d3, -d4), lightPos.z) +
        shadowTexture.SampleCmpLevelZero(sShadowMapSampler, texCoord + float2( d4, -d3), lightPos.z) +
        shadowTexture.SampleCmpLevelZero(sShadowMapSampler, texCoord + float2( d3,  d4), lightPos.z)
        ) / 10.0f;
    return result * result;
}

float GetAttenuation(Light light, float3 wPos)
{
	float attentuation = 1.0f;

	if(light.Type >= LIGHT_POINT)
	{
		float3 L = light.Position - wPos;
		attentuation *= RadialAttenuation(L, light.Range);
		if(light.Type >= LIGHT_SPOT)
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
	if(light.Type == LIGHT_DIRECTIONAL)
	{
		float4 splits = pos.w > cShadowData.CascadeDepths;
		float4 cascades = cShadowData.CascadeDepths > 0;
		int cascadeIndex = dot(splits, cascades);

		const float cascadeFadeTheshold = 0.1f;
		float nextSplit = cShadowData.CascadeDepths[cascadeIndex];
		float splitRange = cascadeIndex == 0 ? nextSplit : nextSplit - cShadowData.CascadeDepths[cascadeIndex - 1];
		float fadeFactor = (nextSplit - pos.w) / splitRange;
		if(fadeFactor <= cascadeFadeTheshold && cascadeIndex != cShadowData.NumCascades - 1)
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
	else if(light.Type == LIGHT_POINT)
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

	float visibility = 1.0f;
	if(light.ShadowIndex >= 0)
	{
#define INLINE_RT_SHADOWS 0
#if INLINE_RT_SHADOWS
		RayDesc ray;
		ray.Origin = wPos + N * 0.01f;
		ray.Direction = light.Position - wPos;
		ray.TMin = 0.001;
		ray.TMax = 1;

		RayQuery<RAY_FLAG_NONE> q;

		q.TraceRayInline(
			tAccelerationStructure,
			RAY_FLAG_NONE,
			~0,
			ray);
		q.Proceed();

		if(q.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
		{
			visibility = 0;
		}
#else
		int shadowIndex = GetShadowIndex(light, pos, wPos);

#define VISUALIZE_CASCADES 0
#if VISUALIZE_CASCADES
		if(light.Type == LIGHT_DIRECTIONAL)
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

		visibility = DoShadow(wPos, shadowIndex, light.InvShadowSize);
		if(visibility <= 0)
		{
			return result;
		}
#endif
	}

	float3 L = normalize(light.Position - wPos);
	if(light.Type == LIGHT_DIRECTIONAL)
	{
		L = -light.Direction;
	}
	result = DefaultLitBxDF(specularColor, roughness, diffuseColor, N, V, L, attenuation);

	float4 color = light.GetColor();
	result.Diffuse *= color.rgb * light.Intensity * visibility;
	result.Specular *= color.rgb * light.Intensity * visibility;

	return result;
}

float HenyeyGreenstrein(float LoV)
{
	const float G = 0.1f;
	float result = 1.0f - G * G;
	result /= (4.0f * PI * pow(1.0f + G * G - (2.0f * G) * LoV, 1.5f));
	return result;
}

float3 ApplyVolumetricLighting(float3 startPoint, float3 endPoint, float4 pos, float4x4 view, Light light, int samples, int frame)
{
	float3 rayVector = endPoint - startPoint;
	float3 ray = normalize(rayVector);
	float3 rayStep = rayVector / samples;
	float3 currentPosition = startPoint;
	float3 accumFog = 0;
		
	float ditherValue = InterleavedGradientNoise(pos.xy, frame);
	currentPosition += rayStep * ditherValue;

	for(int i = 0; i < samples; ++i)
	{
		float attenuation = GetAttenuation(light, currentPosition);
		if(attenuation > 0)
		{
			float visibility = 1.0f;
			if(light.ShadowIndex >= 0)
			{
				int shadowMapIndex = GetShadowIndex(light, pos, currentPosition);
				float4x4 lightViewProjection = cShadowData.LightViewProjections[shadowMapIndex];
				float4 lightPos = mul(float4(currentPosition, 1), lightViewProjection);

				lightPos.xyz /= lightPos.w;
				lightPos.x = lightPos.x * 0.5f + 0.5f;
				lightPos.y = lightPos.y * -0.5f + 0.5f;
				visibility = tTexture2DTable[NonUniformResourceIndex(cShadowData.ShadowMapOffset + shadowMapIndex)].SampleCmpLevelZero(sShadowMapSampler, lightPos.xy, lightPos.z);
			}
			float phase = saturate(HenyeyGreenstrein(dot(ray, light.Direction)));
			accumFog += attenuation * visibility * phase * light.GetColor().rgb * light.Intensity;
		}
		currentPosition += rayStep;
	}
	accumFog /= samples;
	return accumFog;
}