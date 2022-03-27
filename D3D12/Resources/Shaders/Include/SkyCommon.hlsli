#pragma once

#include "Common.hlsli"

float3 GetSky(float3 rayDir)
{
	float3 uv = normalize(rayDir);
	// Bias this a little bit so we don't get pinching at the poles.
	// Proper solution would be a sampler that is 'wrap' on x and 'clamp' on y.
	float uvy = acos(uv.y - sign(uv.y) * 0.001f) / PI;
	float uvx = atan2(uv.x, uv.z) / (2 * PI);
	return SampleLevel2D(cView.SkyIndex, sLinearWrap, float2(uvx, uvy), 0).rgb;
}
