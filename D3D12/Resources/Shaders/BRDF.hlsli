#ifndef __INCLUDE_BRDF__
#define __INCLUDE_BRDF__

#include "Common.hlsli"

/*

MICROFACET SPECULAR MODEL

- N = Macro surface normal
- H = Microfacet normal, aka half-vector ( == normalize(V + L) )
- V = View vector (towards viewer)
- L = Light vector

	Cook Torrence

		D * G * F
	----------------- == D * Vis * F
	  4 * NoL * NoV

				  G
	Vis = ------------------
		    4 * NoL * NoV

- D(H) - Normal Distribution Function - How many microfacets are pointing in the right direction
- G(L, V, H) - Geometry/ShadowMasking function - How many light rays are actually reaching the view
- F(L, H) - Fresnel Reflectance - What fraction of light is reflected as opposed to refracted (How reflective is the surfce)
- Vis - The G term predivided by the denominator of the specular BRDF so they cancel out.

*/

/* DIFFUSE */

// Diffuse BRDF: Lambertian Diffuse
float3 Diffuse_Lambert(float3 diffuseColor)
{
	return diffuseColor * INV_PI;
}

/* SMITH G TERM */

// Smith G1 term (masking function) further optimized for GGX distribution (by substituting G_a into G1_GGX)
float Smith_G1_GGX(float alpha, float NdotS, float alphaSquared, float NdotSSquared) 
{
	return 2.0f / (sqrt(((alphaSquared * (1.0f - NdotSSquared)) + NdotSSquared) / NdotSSquared) + 1.0f);
}

// A fraction G2/G1 where G2 is height correlated can be expressed using only G1 terms
// ["Implementing a Simple Anisotropic Rough Diffuse Material with Stochastic Evaluation", Appendix A by Heitz & Dupuy]
float Smith_G2_Over_G1_Height_Correlated(float alpha, float alphaSquared, float NdotL, float NdotV) 
{
	float G1V = Smith_G1_GGX(alpha, NdotV, alphaSquared, NdotV * NdotV);
	float G1L = Smith_G1_GGX(alpha, NdotL, alphaSquared, NdotL * NdotL);
	return G1L / (G1V + G1L - G1V * G1L);
}

// Appoximation of joint Smith term for GGX
// Returned value is G2 / (4 * NdotL * NdotV). So predivided by specular BRDF denominator
// [Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"]
float Vis_SmithJointApprox(float a2, float NoV, float NoL)
{
	float Vis_SmithV = NoL * (NoV * (1 - a2) + a2);
	float Vis_SmithL = NoV * (NoL * (1 - a2) + a2);
	return 0.5 * rcp(Vis_SmithV + Vis_SmithL);
}

/* NORMAL DISTRIBUTION FUNCTIONS D */

// GGX / Trowbridge-Reitz
// [Walter et al. 2007, "Microfacet models for refraction through rough surfaces"]
float D_GGX(float a2, float NoH)
{
	a2 = max(0.0000001f, a2); // Hack: a2 values of 0 causes NaNs
	float d = (NoH * a2 - NoH) * NoH + 1;
	return a2 / (PI * d * d);
}

/* FRESNEL */

// [Schlick 1994, "An Inexpensive BRDF Model for Physically-Based Rendering"]
float3 F_Schlick(float3 f0, float VoH)
{
	float Fc = Pow5(1.0f - VoH);
	return Fc + (1.0f - Fc) * f0;
}

float3 F_Schlick(float3 f0, float3 f90, float VoH)
{
	float Fc = Pow5(1.0f - VoH);
	return f90 * Fc + (1.0f - Fc) * f0;
}

/* MICROFACET MODEL */

// Samples a microfacet normal for the GGX distribution using VNDF method.
// ["Sampling the GGX Distribution of Visible Normals"] by Heitz
// [https://hal.inria.fr/hal-00996995v1/document and http://jcgt.org/published/0007/04/01/]
// Random variables 'u' must be in <0;1) interval
// PDF is 'G1(NdotV) * D'
float3 SampleGGXVNDF(float3 V, float2 alpha2D, float2 u) 
{
	// Section 3.2: transforming the view direction to the hemisphere configuration
	float3 Vh = normalize(float3(alpha2D.x * V.x, alpha2D.y * V.y, V.z));

	// Section 4.1: orthonormal basis (with special case if cross product is zero)
	float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
	float3 T1 = lensq > 0.0f ? float3(-Vh.y, Vh.x, 0.0f) * rsqrt(lensq) : float3(1.0f, 0.0f, 0.0f);
	float3 T2 = cross(Vh, T1);

	// Section 4.2: parameterization of the projected area
	float r = sqrt(u.x);
	float phi = 2 * PI * u.y;
	float t1 = r * cos(phi);
	float t2 = r * sin(phi);
	float s = 0.5f * (1.0f + Vh.z);
	t2 = lerp(sqrt(1.0f - t1 * t1), t2, s);

	// Section 4.3: reprojection onto hemisphere
	float3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0f, 1.0f - t1 * t1 - t2 * t2)) * Vh;

	// Section 3.4: transforming the normal back to the ellipsoid configuration
	return normalize(float3(alpha2D.x * Nh.x, alpha2D.y * Nh.y, max(0.0f, Nh.z)));
}

float3 SpecularGGX(float specularRoughness, float3 specularColor, float NoL, float NoH, float NoV, float VoH)
{
	float a2 = Pow4(specularRoughness);

	// Generalized microfacet specular
	float D = D_GGX(a2, NoH);
	float Vis = Vis_SmithJointApprox(a2, NoV, NoL);
	float3 F = F_Schlick(specularColor, VoH);

	return (D * Vis) * F;
}

#endif
