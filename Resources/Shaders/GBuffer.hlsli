#pragma once

#include "Common.hlsli"

struct GBufferData
{
	float3 BaseColor;
	float3 Emissive;
	float Roughness;
	float Metalness;
	float Specular;
	float3 Normal;
};

GBufferData LoadGBuffer(uint4 data)
{
	GBufferData gbuffer;
	float4 baseColorSpecular 	= RGBA8_UNORM::Unpack(data.x);
	gbuffer.BaseColor 			= baseColorSpecular.xyz;
	gbuffer.Specular 			= baseColorSpecular.w;
	float2 roughnessMetalness 	= RG16_UNORM::Unpack(data.y);
	gbuffer.Roughness 			= roughnessMetalness.x;
	gbuffer.Metalness 			= roughnessMetalness.y;
	gbuffer.Normal 				= Octahedral::Unpack(RG16_SNORM::Unpack(data.z));
	gbuffer.Emissive 			= R9G9B9E5_SHAREDEXP::Unpack(data.w);
	return gbuffer;
}

uint4 PackGBuffer(GBufferData data)
{
	return uint4(
		RGBA8_UNORM::Pack(float4(data.BaseColor, data.Specular)),
		RG16_UNORM::Pack(float2(data.Roughness, data.Metalness)),
		RG16_SNORM::Pack(Octahedral::Pack(data.Normal)),
		R9G9B9E5_SHAREDEXP::Pack(data.Emissive)
	);
}
