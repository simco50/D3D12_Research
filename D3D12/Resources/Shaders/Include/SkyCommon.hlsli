#pragma once

#include "CommonBindings.hlsli"
#include "Atmosphere.hlsli"

float3 GetSky(float3 rayDir)
{
	float3 rayStart = cView.ViewPosition.xyz;
	float rayLength = 1000000.0f;
	if(0)
	{
		float2 planetIntersection = PlanetIntersection(rayStart, rayDir);
		if(planetIntersection.x > 0)
		{
			rayLength = min(rayLength, planetIntersection.x);
		}
	}
	Light sun = GetLight(0);
	float3 lightDir = -sun.Direction;
	float3 lightColor = sun.GetColor().rgb;

	float3 transmittance;
	return IntegrateScattering(rayStart, rayDir, rayLength, lightDir, lightColor, transmittance);
}
