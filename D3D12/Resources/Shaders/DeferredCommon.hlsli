#pragma once

#include "Common.hlsli"

void UnpackGBuffer0(float4 data, out float3 baseColor, out float roughness)
{
	baseColor = data.xyz;
	roughness = data.w;
}

float4 PackGBuffer0(float3 baseColor, float roughness)
{
	return float4(baseColor, roughness);
}

void UnpackGBuffer1(float4 data, out float3 normal, out float metalness)
{
	normal = data.xyz * 2.0f - 1.0f;
	metalness = data.w;
}

float4 PackGBuffer1(float3 normal, float metalness)
{
	return float4(normal * 0.5f + 0.5f, metalness);
}
