#pragma once

#include "CommonBindings.hlsli"

float3 GetSky(float3 rayDir)
{
	float3 uv = normalize(rayDir);
	float uvy = acos(uv.y) / PI;
	float uvx = atan2(uv.x, uv.z) / (2 * PI);
	return SampleLevel2D(cView.SkyIndex, sLinearWrap, float2(uvx, uvy), 0).rgb;
}
