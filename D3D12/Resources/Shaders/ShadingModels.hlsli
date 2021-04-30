#ifndef __INCLUDE_SHADING_MODELS__
#define __INCLUDE_SHADING_MODELS__

#include "Common.hlsli"

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

// Diffuse BRDF: Lambertian Diffuse
float3 Diffuse_Lambert(float3 diffuseColor)
{
	return diffuseColor * INV_PI;
}

// GGX / Trowbridge-Reitz
// [Walter et al. 2007, "Microfacet models for refraction through rough surfaces"]
float D_GGX(float a2, float NoH)
{
	a2 = max(0.0000001f, a2);
	float d = (NoH * a2 - NoH) * NoH + 1;	// 2 mad
	return a2 / (PI * d * d);				// 4 mul, 1 rcp
}

// Appoximation of joint Smith term for GGX
// [Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"]
float Vis_SmithJointApprox(float a2, float NoV, float NoL)
{
	float Vis_SmithV = NoL * (NoV * (1 - a2) + a2);
	float Vis_SmithL = NoV * (NoL * (1 - a2) + a2);
	return 0.5 * rcp(Vis_SmithV + Vis_SmithL);
}

// [Schlick 1994, "An Inexpensive BRDF Model for Physically-Based Rendering"]
float3 F_Schlick(float3 f0, float VoH)
{
	float Fc = Pow5(1 - VoH);		// 1 sub, 3 mul
	return Fc + (1 - Fc) * f0;		// 1 add, 3 mad
}

struct LightResult
{
	float3 Diffuse;
	float3 Specular;
};

//Microfacet Specular Model:
/*
	Cook Torrence

		D * G * F
	----------------- == D * Vis * F
	  4 * NoL * NoV

				  G
	Vis = ------------------
		    4 * NoL * NoV

- F(l, h) - Fresnel Reflectance - What fraction of light is reflected as opposed to refracted (How reflective is the surfce)
- D(h) - Normal Distribution Function - How many microfacets are pointing in the right direction
- G(l, v, h) - Geometry/ShadowMasking function - How many light rays are actually reaching the view
*/

float3 SpecularGGX(float specularRoughness, float3 specularColor, float NoL, float NoH, float NoV, float VoH)
{
	float a2 = Pow4(specularRoughness);

	// Generalized microfacet specular
	float D = D_GGX(a2, NoH);
	float Vis = Vis_SmithJointApprox(a2, NoV, NoL);
	float3 F = F_Schlick(specularColor, VoH);

	return (D * Vis) * F;
}

LightResult DefaultLitBxDF(float3 specularColor, float specularRoughness, float3 diffuseColor, half3 N, half3 V, half3 L, float falloff)
{
	LightResult lighting = (LightResult)0;
	if(falloff <= 0)
		return lighting;
	float NoL = saturate(dot(N, L));
	if(NoL == 0)
		return lighting;

	float3 H = normalize(V + L);
	float NoV = saturate(abs(dot(N, V)) + 1e-5);
	float NoH = saturate(dot(N, H));
	float VoH = saturate(dot(V, H));
	NoV = saturate(abs(NoV) + 1e-5); // Bias to avoid NaNs

	lighting.Diffuse  = (falloff * NoL) * Diffuse_Lambert(diffuseColor);
	lighting.Specular = (falloff * NoL) * SpecularGGX(specularRoughness, specularColor, NoL, NoH, NoV, VoH);
	return lighting;
}

#endif