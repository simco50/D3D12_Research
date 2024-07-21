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

void UnpackGBuffer1(float2 data, out float3 normal)
{
	normal = Octahedral::Unpack(data * 2.0f - 1.0f);
}

float2 PackGBuffer1(float3 normal)
{
	return Octahedral::Pack(normal) * 0.5f + 0.5f;
}

void UnpackGBuffer2(float2 data, out float roughness, out float metalness)
{
	roughness = data.x;
	metalness = data.y;
}

float2 PackGBuffer2(float roughness, float metalness)
{
	return float2(roughness, metalness);
}
