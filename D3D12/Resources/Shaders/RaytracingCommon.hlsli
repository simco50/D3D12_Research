#ifndef __INCLUDE_RAYTRACING_COMMON__
#define __INCLUDE_RAYTRACING_COMMON__

#include "SkyCommon.hlsli"

#define RAY_BIAS 1.0e-2f
#define RAY_MAX_T 1.0e10f

struct RayCone
{
    float Width;
    float SpreadAngle;
};

RayCone PropagateRayCone(RayCone cone, float surfaceSpreadAngle, float hitT)
{
    RayCone newCone;
    newCone.Width = cone.SpreadAngle * hitT + cone.Width;
    newCone.SpreadAngle = cone.SpreadAngle + surfaceSpreadAngle;
    return newCone;
}

// Texture Level of Detail Strategies for Real-Time Ray Tracing
// Ray Tracing Gems - Tomas Akenine-Möller
float ComputeRayConeMip(RayCone cone, float3 vertexNormal, float2 vertexUVs[3], float2 textureDimensions)
{   
    // Triangle surface area
    float3 normal = vertexNormal;
    float invWorldArea = rsqrt(dot(normal, normal));
    float3 triangleNormal = abs(normal * invWorldArea);

    // UV area
    float2 duv0 = vertexUVs[2] - vertexUVs[0];
    float2 duv1 = vertexUVs[1] - vertexUVs[0];
    float uvArea = 0.5f * length(cross(float3(duv0, 0), float3(duv1, 0)));

	float triangleLODConstant = 0.5f * log2(uvArea * invWorldArea);

	float lambda = triangleLODConstant;
	lambda += log2(abs(cone.Width));
	lambda += 0.5f * log2(textureDimensions.x * textureDimensions.y);
	lambda -= log2(abs(dot(WorldRayDirection(), triangleNormal)));
	return lambda;
}

float3 DefaultSkyBackground()
{
    return CIESky(WorldRayDirection(), -tLights[0].Direction);
}

#endif