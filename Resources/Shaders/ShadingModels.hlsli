#pragma once

#include "BRDF.hlsli"

// 0.08 is a max F0 we define for dielectrics which matches with Crystalware and gems (0.05 - 0.08)
// This means we cannot represent Diamond-like surfaces as they have an F0 of 0.1 - 0.2
float DielectricSpecularToF0(float specular)
{
	return 0.08f * specular;
}

// Cool list of IOR values for different surfaces
// https://pixelandpoly.com/ior.html
float IORToF0(float IOR)
{
	return Square((IOR - 1) / (IOR + 1));
}

float IORToSpecular(float IOR)
{
	return IORToF0(IOR) / 0.08f;
}

//Note from Filament: vec3 f0 = 0.16 * reflectance * reflectance * (1.0 - metallic) + baseColor * metallic;
// F0 is the base specular reflectance of a surface
// For dielectrics, this is monochromatic commonly between 0.02 (water) and 0.08 (gems) and derived from a separate specular value
// For conductors, this is based on the base color we provided
float3 ComputeF0(float specular, float3 baseColor, float metalness)
{
	return lerp(DielectricSpecularToF0(specular).xxx, baseColor, metalness);
}

float3 ComputeDiffuseColor(float3 baseColor, float metalness)
{
	return baseColor * (1 - metalness);
}

struct LightResult
{
	float3 Diffuse;
	float3 Specular;

	LightResult operator+(LightResult rhs)
	{
		LightResult result;
		result.Diffuse = Diffuse + rhs.Diffuse;
		result.Specular = Specular + rhs.Specular;
		return result;
	}
};

LightResult DefaultLitBxDF(float3 specularColor, float specularRoughness, float3 diffuseColor, half3 N, half3 V, half3 L, float falloff)
{
	LightResult lighting = (LightResult)0;
	if(falloff <= 0.0f)
	{
		return lighting;
	}

	float NdotL = saturate(dot(N, L));
	if(NdotL == 0.0f)
	{
		return lighting;
	}

	float3 H = normalize(V + L);
	float NdotV = saturate(abs(dot(N, V)) + 1e-5); // Bias to avoid artifacting
	float NdotH = saturate(dot(N, H));
	float VdotH = saturate(dot(V, H));

	// Generalized microfacet Specular BRDF
	float a = Square(specularRoughness);
	float a2 = clamp(Square(a), 0.0001f, 1.0f);
	float D = D_GGX(a2, NdotH);
	float Vis = Vis_SmithJointApprox(a2, NdotV, NdotL);
	float3 F = F_Schlick(specularColor, VdotH);
	lighting.Specular = (falloff * NdotL) * (D * Vis) * F;

	// Kulla17 - Energy conervation due to multiple scattering
#if 0
	float gloss = Pow4(1 - specularRoughness);
	float3 DFG = EnvDFGPolynomial(specularColor, gloss, NdotV);
	float3 energyCompensation = 1.0f + specularColor * (1.0f / DFG.y - 1.0f);
	lighting.Specular *= energyCompensation;
#endif

	// Diffuse BRDF
	lighting.Diffuse = (falloff * NdotL) * Diffuse_Lambert(diffuseColor);

	return lighting;
}
