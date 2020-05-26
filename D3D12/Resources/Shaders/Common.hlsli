#include "Constants.hlsli"

float4 UIntToColor(uint c)
{
    return float4(
            (float)((c & 0x00FF0000) >> 16) / 255.0f,
            (float)((c & 0x0000FF00) >> 8) / 255.0f,
            (float)((c & 0x000000FF) >> 0) / 255.0f,
            (float)((c & 0xFF000000) >> 24) / 255.0f
        );
}

struct Light
{
	float3 Position;
	int Enabled;
	float3 Direction;
	int Type;
	float2 SpotlightAngles;
	uint Color;
    float Intensity;
	float Range;
    int ShadowIndex;

    float4 GetColor()
    {
        return UIntToColor(Color);
    }
};

struct Plane
{
    float3 Normal;
    float DistanceToOrigin;
};

struct Frustum
{
    Plane Planes[4];
};

struct Sphere
{
    float3 Position;
    float Radius;
};

struct Cone
{
    float3 Tip;
    float Height;
    float3 Direction;
    float Radius;
};

struct AABB
{
    float4 Center;
    float4 Extents;
};

bool SphereInAABB(Sphere sphere, AABB aabb)
{
    float3 d = max(0, abs(aabb.Center.xyz - sphere.Position) - aabb.Extents.xyz);
    float distanceSq = dot(d, d);
    return distanceSq <= sphere.Radius * sphere.Radius;
}

bool SphereBehindPlane(Sphere sphere, Plane plane)
{
    return dot(plane.Normal, sphere.Position) - plane.DistanceToOrigin < -sphere.Radius;
}

bool PointBehindPlane(float3 p, Plane plane)
{
    return dot(plane.Normal, p) - plane.DistanceToOrigin < 0;
}

bool ConeBehindPlane(Cone cone, Plane plane)
{
    float3 furthestPointDirection = cross(cross(plane.Normal, cone.Direction), cone.Direction);
    float3 furthestPointOnCircle = cone.Tip + cone.Direction * cone.Height - furthestPointDirection * cone.Radius;
    return PointBehindPlane(cone.Tip, plane) && PointBehindPlane(furthestPointOnCircle, plane);
}

bool ConeInFrustum(Cone cone, Frustum frustum, float zNear, float zFar)
{
    Plane nearPlane, farPlane;
    nearPlane.Normal = float3(0, 0, 1);
    nearPlane.DistanceToOrigin = zNear;
    farPlane.Normal = float3(0, 0, -1);
    farPlane.DistanceToOrigin = -zFar;
 
    bool inside = !(ConeBehindPlane(cone, nearPlane) || ConeBehindPlane(cone, farPlane));
    for(int i = 0; i < 4 && inside; ++i)
    {
        inside = !ConeBehindPlane(cone, frustum.Planes[i]);
    }
    return inside;
}

bool SphereInFrustum(Sphere sphere, Frustum frustum, float depthNear, float depthFar)
{
    bool inside = !(sphere.Position.z + sphere.Radius < depthNear || sphere.Position.z - sphere.Radius > depthFar);
    for(int i = 0; i < 4 && inside; ++i)
    {
        inside = !SphereBehindPlane(sphere, frustum.Planes[i]);
    }
    return inside;
}

Plane CalculatePlane(float3 a, float3 b, float3 c)
{
    float3 v0 = b - a;
    float3 v1 = c - a;
    
    Plane plane;
    plane.Normal = normalize(cross(v1, v0));
    plane.DistanceToOrigin = dot(plane.Normal, a);
    return plane;
}

// Convert clip space coordinates to view space
float4 ClipToView(float4 clip, float4x4 projectionInverse)
{
    // View space position.
    float4 view = mul(clip, projectionInverse);
    // Perspective projection.
    view = view / view.w;
    return view;
}
 
// Convert screen space coordinates to view space.
float4 ScreenToView(float4 screen, float2 screenDimensionsInv, float4x4 projectionInverse)
{
    // Convert to normalized texture coordinates
    float2 texCoord = screen.xy * screenDimensionsInv;
    // Convert to clip space
    float4 clip = float4(float2(texCoord.x, 1.0f - texCoord.y) * 2.0f - 1.0f, screen.z, screen.w);
    return ClipToView(clip, projectionInverse);
}

float3 WorldFromDepth(float2 uv, float depth, float4x4 viewProjectionInverse)
{
    float4 clip = float4(float2(uv.x, 1.0f - uv.y) * 2.0f - 1.0f, depth, 1.0f);
    float4 world = mul(clip, viewProjectionInverse);
    return world.xyz / world.w;
}

float LinearizeDepth(float z, float near, float far)
{
    float z_n = 2.0 * z - 1.0;
	return 2.0 * far * near / (near + far - z_n * (near - far));
}

void AABBFromMinMax(inout AABB aabb, float3 minimum, float3 maximum)
{
    aabb.Center = float4((minimum + maximum) / 2.0f, 0);
    aabb.Extents = float4(maximum, 0) - aabb.Center;
}

//Emulates sampling a TextureCube
float2 SampleCube(const float3 v, out float faceIndex)
{
	float3 vAbs = abs(v);
	float ma;
	float2 uv;
	if(vAbs.z >= vAbs.x && vAbs.z >= vAbs.y)
	{
		faceIndex = v.z < 0.0 ? 5.0 : 4.0;
		ma = 0.5 / vAbs.z;
		uv = float2(v.z < 0.0 ? -v.x : v.x, -v.y);
	}
	else if(vAbs.y >= vAbs.x)
	{
		faceIndex = v.y < 0.0 ? 3.0 : 2.0;
		ma = 0.5 / vAbs.y;
		uv = float2(v.x, v.y < 0.0 ? -v.z : v.z);
	}
	else
	{
		faceIndex = v.x < 0.0 ? 1.0 : 0.0;
		ma = 0.5 / vAbs.x;
		uv = float2(v.x < 0.0 ? v.z : -v.z, -v.y);
	}
	return uv * ma + 0.5;
}

float GetCubeFaceIndex(const float3 v)
{
	float3 vAbs = abs(v);
	float faceIndex = 0;
	if(vAbs.z >= vAbs.x && vAbs.z >= vAbs.y)
	{
		faceIndex = v.z < 0.0 ? 5.0 : 4.0;
	}
	else if(vAbs.y >= vAbs.x)
	{
		faceIndex = v.y < 0.0 ? 3.0 : 2.0;
	}
	else
	{
		faceIndex = v.x < 0.0 ? 1.0 : 0.0;
	}
    return faceIndex;
}

float Pow4(float x)
{
	float xx = x * x;
	return xx * xx;
}

float Pow5(float x)
{
	float xx = x * x;
	return xx * xx * x;
}

float Square(float x)
{
    return x * x;
}

//This is still not totally exact as pow() has imprecisions
float SrgbToLinear(float y)
{
    if(y <= 0.04045f)
    {
        return y / 12.92f;
    }
    return pow((y + 0.055f) / 1.055f, 2.4f);
}

float SrgbToLinearFast(float y)
{
    return pow(y, 2.2f);
}

//This is still not totally exact as pow() has imprecisions
float LinearToSrgb(float x)
{
    if(x <= 0.00313008)
    {
        return 12.92f * x;
    }
    return 1.055f * pow(x, 1.0f/ 2.4f) - 0.055f;
}

float LinearToSrgbFast(float x)
{
    return pow(x, 1.0f / 2.2f);
}

float3 LinearToSrgbFast(float3 rgb)
{
    return pow(rgb, 1.0f / 2.2f);
}