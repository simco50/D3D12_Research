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

Ray GeneratePinholeCameraRay(float2 pixel, float4x4 viewInverse, float4x4 projection)
{
    // Set up the ray.
    Ray ray;
    ray.Origin = viewInverse[3].xyz;
    // Extract the aspect ratio and fov from the projection matrix.
    float aspect = projection[1][1] / projection[0][0];
    float tanHalfFovY = 1.f / projection[1][1];

    // Compute the ray direction.
    ray.Direction = normalize(
        (pixel.x * viewInverse[0].xyz * tanHalfFovY * aspect) -
        (pixel.y * viewInverse[1].xyz * tanHalfFovY) +
        viewInverse[2].xyz);

    return ray;
}

// Ray Tracing Gems: A Fast and Robust Method for Avoiding Self-Intersection
// Wächter and Binder
// Offset ray so that it never self-intersects
float3 OffsetRay(float3 position, float3 geometryNormal)
{
    static const float origin = 1.0f / 32.0f;
    static const float float_scale = 1.0f / 65536.0f;
    static const float int_scale = 256.0f;

    int3 of_i = int3(int_scale * geometryNormal.x, int_scale * geometryNormal.y, int_scale * geometryNormal.z);

    float3 p_i = float3(
        asfloat(asint(position.x) + ((position.x < 0) ? -of_i.x : of_i.x)),
        asfloat(asint(position.y) + ((position.y < 0) ? -of_i.y : of_i.y)),
        asfloat(asint(position.z) + ((position.z < 0) ? -of_i.z : of_i.z)));

    return float3(abs(position.x) < origin ? position.x + float_scale * geometryNormal.x : p_i.x,
        abs(position.y) < origin ? position.y + float_scale * geometryNormal.y : p_i.y,
        abs(position.z) < origin ? position.z + float_scale * geometryNormal.z : p_i.z);
}

// Calculates rotation quaternion from input vector to the vector (0, 0, 1)
// Input vector must be normalized!
float4 GetRotationToZAxis(float3 input) 
{
    // Handle special case when input is exact or near opposite of (0, 0, 1)
    if (input.z < -0.99999f)
    {
        return float4(1.0f, 0.0f, 0.0f, 0.0f);
    }
    return normalize(float4(input.y, -input.x, 0.0f, 1.0f + input.z));
}

// Returns the quaternion with inverted rotation
float4 InvertRotation(float4 q)
{
    return float4(-q.x, -q.y, -q.z, q.w);
}

// Optimized point rotation using quaternion
// Source: https://gamedev.stackexchange.com/questions/28395/rotating-vector3-by-a-quaternion
float3 RotatePoint(float4 q, float3 v) 
{
    float3 qAxis = float3(q.x, q.y, q.z);
    return 2.0f * dot(qAxis, v) * qAxis + (q.w * q.w - dot(qAxis, qAxis)) * v + 2.0f * q.w * cross(qAxis, v);
}

#endif