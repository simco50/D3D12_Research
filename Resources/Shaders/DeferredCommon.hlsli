#pragma once

#include "Common.hlsli"

void UnpackGBuffer0(float4 data, out float3 baseColor, out float specular)
{
	baseColor = data.xyz;
	specular = data.w;
}

float4 PackGBuffer0(float3 baseColor, float specular)
{
	return float4(baseColor, specular);
}

void UnpackGBuffer1(float4 data, out float3 normal, out float roughness, out float metalness)
{
	normal = Octahedral::Unpack(data.xy * 2.0f - 1.0f);
	roughness = data.z;
	metalness = data.w;
}

float4 PackGBuffer1(float3 normal, float roughness, float metalness)
{
	return float4(Octahedral::Pack(normal) * 0.5f + 0.5f, roughness, metalness);
}
