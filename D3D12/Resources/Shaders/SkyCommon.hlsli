#pragma once

#include "Common.hlsli"

float3 GetSky(float3 rayDir)
{
	float3 uv = normalize(rayDir);
	TextureCube<float4> skyTexture = ResourceDescriptorHeap[cView.SkyIndex];
	return skyTexture.SampleLevel(sLinearWrap, uv, 0).rgb;
}
