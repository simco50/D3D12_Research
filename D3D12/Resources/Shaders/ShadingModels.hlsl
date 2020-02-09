#include "Common.hlsl"

float Pow4( float x )
{
	float xx = x*x;
	return xx * xx;
}

float Pow5( float x )
{
	float xx = x*x;
	return xx * xx * x;
}

float3 Diffuse_Lambert(float3 DiffuseColor)
{
	return DiffuseColor * (1 / PI);
}

// GGX / Trowbridge-Reitz
// [Walter et al. 2007, "Microfacet models for refraction through rough surfaces"]
float D_GGX( float a2, float NoH )
{
	float d = ( NoH * a2 - NoH ) * NoH + 1;	// 2 mad
	return a2 / ( PI*d*d );					// 4 mul, 1 rcp
}

// Appoximation of joint Smith term for GGX
// [Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"]
float Vis_SmithJointApprox( float a2, float NoV, float NoL )
{
	float a = sqrt(a2);
	float Vis_SmithV = NoL * ( NoV * ( 1 - a ) + a );
	float Vis_SmithL = NoV * ( NoL * ( 1 - a ) + a );
	return 0.5 * rcp( Vis_SmithV + Vis_SmithL );
}

// [Schlick 1994, "An Inexpensive BRDF Model for Physically-Based Rendering"]
float3 F_Schlick( float3 SpecularColor, float VoH )
{
	float Fc = Pow5( 1 - VoH );					// 1 sub, 3 mul
	//return Fc + (1 - Fc) * SpecularColor;		// 1 add, 3 mad
	
	// Anything less than 2% is physically impossible and is instead considered to be shadowing
	return saturate( 50.0 * SpecularColor.g ) * Fc + (1 - Fc) * SpecularColor;
}

struct LightResult
{
	float3 Diffuse;
	float3 Specular;
};

float3 SpecularGGX(float Roughness, float3 SpecularColor, float NoL, float NoH, float NoV, float VoH)
{
	float a2 = Pow4(Roughness);
	
	// Generalized microfacet specular
	float D = D_GGX(a2, NoH);
	float Vis = Vis_SmithJointApprox(a2, NoV, NoL);
	float3 F = F_Schlick(SpecularColor, VoH);

	return (D * Vis) * F;
}

LightResult DefaultLitBxDF(float3 SpecularColor, float Roughness, float3 DiffuseColor, half3 N, half3 V, half3 L, float Falloff)
{
	float3 H = normalize(V + L);
	float NoH = saturate(dot(N, H));
	float NoL = saturate(dot(N, L));
	float NoV = dot(N, V);
	float VoH = saturate(dot(V, H));
	NoV = saturate(abs(NoV) + 1e-5);

	LightResult Lighting;
	Lighting.Diffuse  = (Falloff * NoL) * Diffuse_Lambert(DiffuseColor);
	Lighting.Specular = (Falloff * NoL) * SpecularGGX(Roughness, SpecularColor, NoL, NoH, NoV, VoH);
	return Lighting;
}