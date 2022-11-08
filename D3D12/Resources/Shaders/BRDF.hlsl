#include "Constants.hlsli"

struct Lighting
{
    float3 Diffuse;
    float3 Specular;
};

struct BxDFContext
{
	float NoV;
	float NoL;
	float VoL;
	float NoH;
	float VoH;
};

float Pow5( float x )
{
	float xx = x*x;
	return xx * xx * x;
}

float Pow4( float x )
{
	float xx = x*x;
	return xx * xx;
}


void Init( inout BxDFContext Context, half3 N, half3 V, half3 L )
{
	Context.NoL = dot(N, L);
	Context.NoV = dot(N, V);
	Context.VoL = dot(V, L);
	float InvLenH = rsqrt( 2 + 2 * Context.VoL );
	Context.NoH = saturate( ( Context.NoL + Context.NoV ) * InvLenH );
	Context.VoH = saturate( InvLenH + InvLenH * Context.VoL );
}

float3 Diffuse_Lambert( float3 DiffuseColor )
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

float3 SpecularGGX(float roughness, float3 specularColor, BxDFContext context)
{
	float a2 = Pow4(roughness);

	// Generalized microfacet specular
	float D = D_GGX(a2, context.NoH);
	float Vis = Vis_SmithJointApprox(a2, context.NoV, context.NoL);
	float3 F = F_Schlick(specularColor, context.VoH);

	return (D * Vis) * F;
}

Lighting DefaultBRDF(float3 diffuseColor, float3 specularColor, float roughness, float3 normal, float3 view, float3 lightDir)
{
	Lighting lighting;

    BxDFContext context;
    Init(context, normal, view, lightDir);

	lighting.Diffuse = context.NoL * Diffuse_Lambert(diffuseColor);
	lighting.Specular = context.NoL * SpecularGGX(roughness, specularColor, context);
    return lighting;
}

float4 PSMain() : SV_TARGET0
{
	return float4(1,1,1,1);
}