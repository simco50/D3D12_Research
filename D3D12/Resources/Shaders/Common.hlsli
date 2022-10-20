#pragma once

#include "CommonBindings.hlsli"
#include "Packing.hlsli"

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

struct Ray
{
	float3 Origin;
	float3 Direction;
};

struct MaterialProperties
{
	float3 BaseColor;
	float3 NormalTS;
	float Metalness;
	float3 Emissive;
	float Roughness;
	float Opacity;
	float Specular;
};

template<typename T>
T InverseLerp(T value, T minValue, T maxValue)
{
	return (value - minValue) / (maxValue - minValue);
}

template<typename T>
T Remap(T value, T inRangeMin, T inRangeMax, T outRangeMin, T outRangeMax)
{
	T t = InverseLerp(value, inRangeMin, inRangeMax);
	return lerp(outRangeMin, outRangeMax, t);
}

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

// Convert clip space (-1, 1) coordinates to view space
float3 ClipToView(float4 clip, float4x4 projectionInverse)
{
	// View space position.
	float4 view = mul(clip, projectionInverse);
	// Perspective projection.
	view = view / view.w;
	return view.xyz;
}

// Convert view space position to screen UVs (0, 1). Non-linear Z
float3 ViewToWindow(float3 view, float4x4 projection)
{
	float4 proj = mul(float4(view, 1), projection);
	proj.xyz /= proj.w;
	proj.x = (proj.x + 1) / 2;
	proj.y = 1 - (proj.y + 1) / 2;
	return proj.xyz;
}

float3 ViewFromDepth(float2 uv, float depth, float4x4 projectionInverse)
{
	float4 clip = float4(float2(uv.x, 1.0f - uv.y) * 2.0f - 1.0f, depth, 1.0f);
	return ClipToView(clip, projectionInverse);
}

float3 WorldFromDepth(float2 uv, float depth, float4x4 viewProjectionInverse)
{
	float4 clip = float4(float2(uv.x, 1.0f - uv.y) * 2.0f - 1.0f, depth, 1.0f);
	float4 world = mul(clip, viewProjectionInverse);
	return world.xyz / world.w;
}

#if 1
float3 NormalFromDepth(Texture2D depthTexture, SamplerState depthSampler, float2 uv, float2 invDimensions, float4x4 inverseProjection)
{
	float3 vpos0 = ViewFromDepth(uv, depthTexture.SampleLevel(depthSampler, uv, 0).x, inverseProjection);
	float3 vpos1 = ViewFromDepth(uv + float2(1, 0) * invDimensions, depthTexture.SampleLevel(depthSampler, uv + float2(1, 0) * invDimensions, 0).x, inverseProjection);
	float3 vpos2 = ViewFromDepth(uv + float2(0, -1) * invDimensions, depthTexture.SampleLevel(depthSampler, uv + float2(0, -1) * invDimensions, 0).x, inverseProjection);
	float3 normal = normalize(cross(vpos2 - vpos0, vpos1 - vpos0));
	return normal;
}
#elif 0
// János Turánszki' - Improved Normal Reconstruction
// https://wickedengine.net/2019/09/22/improved-normal-reconstruction-from-depth/
float3 NormalFromDepth(Texture2D depthTexture, SamplerState depthSampler, float2 uv, float2 invDimensions, float4x4 inverseProjection)
{
	float3 vposc = ViewFromDepth(uv, depthTexture.SampleLevel(depthSampler, uv, 0).x, inverseProjection);
	float3 vposl = ViewFromDepth(uv + float2(-1, 0) * invDimensions, depthTexture.SampleLevel(depthSampler, uv + float2(-1, 0) * invDimensions, 0).x, inverseProjection);
	float3 vposr = ViewFromDepth(uv + float2(1, 0) * invDimensions, depthTexture.SampleLevel(depthSampler, uv + float2(1, 0) * invDimensions, 0).x, inverseProjection);
	float3 vposd = ViewFromDepth(uv + float2(0, -1) * invDimensions, depthTexture.SampleLevel(depthSampler, uv + float2(0, -1) * invDimensions, 0).x, inverseProjection);
	float3 vposu = ViewFromDepth(uv + float2(0, 1) * invDimensions, depthTexture.SampleLevel(depthSampler, uv + float2(0, 1) * invDimensions, 0).x, inverseProjection);

	float3 l = vposc - vposl;
	float3 r = vposr - vposc;
	float3 d = vposc - vposd;
	float3 u = vposu - vposc;

	float3 hDeriv = abs(l.z) < abs(r.z) ? l : r;
	float3 vDeriv = abs(d.z) < abs(u.z) ? d : u;

	float3 viewNormal = normalize(cross(hDeriv, vDeriv));
	return viewNormal;
}
#elif 0
// Yuwen Wu - Accurate Normal Reconstruction
// https://atyuwen.github.io/posts/normal-reconstruction/
float3 NormalFromDepth(Texture2D depthTexture, SamplerState depthSampler, float2 uv, float2 invDimensions, float4x4 inverseProjection)
{
	float c = depthTexture.SampleLevel(depthSampler, uv, 0).x;

	float3 vposc = ViewFromDepth(uv, depthTexture.SampleLevel(depthSampler, uv, 0).x, inverseProjection);
	float3 vposl = ViewFromDepth(uv + float2(-1, 0) * invDimensions, depthTexture.SampleLevel(depthSampler, uv + float2(-1, 0) * invDimensions, 0).x, inverseProjection);
	float3 vposr = ViewFromDepth(uv + float2(1, 0) * invDimensions, depthTexture.SampleLevel(depthSampler, uv + float2(1, 0) * invDimensions, 0).x, inverseProjection);
	float3 vposd = ViewFromDepth(uv + float2(0, -1) * invDimensions, depthTexture.SampleLevel(depthSampler, uv + float2(0, -1) * invDimensions, 0).x, inverseProjection);
	float3 vposu = ViewFromDepth(uv + float2(0, 1) * invDimensions, depthTexture.SampleLevel(depthSampler, uv + float2(0, 1) * invDimensions, 0).x, inverseProjection);

	float3 l = vposc - vposl;
	float3 r = vposr - vposc;
	float3 d = vposc - vposd;
	float3 u = vposu - vposc;

	// get depth values at 1 & 2 pixels offsets from current along the horizontal axis
	float4 H = float4(
		depthTexture.SampleLevel(depthSampler, uv + float2(-1.0, 0.0) * invDimensions, 0).x,
		depthTexture.SampleLevel(depthSampler, uv + float2( 1.0, 0.0) * invDimensions, 0).x,
		depthTexture.SampleLevel(depthSampler, uv + float2(-2.0, 0.0) * invDimensions, 0).x,
		depthTexture.SampleLevel(depthSampler, uv + float2( 2.0, 0.0) * invDimensions, 0).x
	);

	// get depth values at 1 & 2 pixels offsets from current along the vertical axis
	float4 V = float4(
		depthTexture.SampleLevel(depthSampler, uv + float2(0.0,-1.0) * invDimensions, 0).x,
		depthTexture.SampleLevel(depthSampler, uv + float2(0.0, 1.0) * invDimensions, 0).x,
		depthTexture.SampleLevel(depthSampler, uv + float2(0.0,-2.0) * invDimensions, 0).x,
		depthTexture.SampleLevel(depthSampler, uv + float2(0.0, 2.0) * invDimensions, 0).x
	);

	// current pixel's depth difference from slope of offset depth samples
	// differs from original article because we're using non-linear depth values
	// see article's comments
	float2 he = abs((2 * H.xy - H.zw) - c);
	float2 ve = abs((2 * V.xy - V.zw) - c);

	// pick horizontal and vertical diff with the smallest depth difference from slopes
	float3 hDeriv = he.x < he.y ? l : r;
	float3 vDeriv = ve.x < ve.y ? d : u;

	// get view space normal from the cross product of the best derivatives
	float3 viewNormal = normalize(cross(hDeriv, vDeriv));
	return viewNormal;
}
#endif

// Convert screen space coordinates (0, width/height) to view space.
float3 ScreenToView(float4 screen, float2 screenDimensionsInv, float4x4 projectionInverse)
{
	// Convert to normalized texture coordinates
	float2 screenNormalized = screen.xy * screenDimensionsInv;
	return ViewFromDepth(screenNormalized, screen.z, projectionInverse);
}

//View space depth [0, 1]
float LinearizeDepth01(float z, float near, float far)
{
	return far / (far + z * (near - far));
}
float LinearizeDepth01(float z)
{
	return cView.FarZ / (cView.FarZ + z * (cView.NearZ - cView.FarZ));
}

//View space depth [0, far plane]
float LinearizeDepth(float z, float near, float far)
{
	return near * LinearizeDepth01(z, near, far);
}
float LinearizeDepth(float z)
{
	return cView.NearZ * LinearizeDepth01(z, cView.NearZ, cView.FarZ);
}

// View space depth [0, far plane] to NDC [0, 1]
float LinearDepthToNDC(float z, float4x4 projection)
{
	return (z * projection[2][2] + projection[3][2]) / z;
}

void AABBFromMinMax(inout AABB aabb, float3 minimum, float3 maximum)
{
	aabb.Center = float4((minimum + maximum) / 2.0f, 0);
	aabb.Extents = float4(maximum, 0) - aabb.Center;
}

template<typename T>
T Pow4(T x)
{
	T xx = x * x;
	return xx * xx;
}

template<typename T>
T Pow5(T x)
{
	T xx = x * x;
	return xx * xx * x;
}

template<typename T>
T Square(T x)
{
	return x * x;
}

template<typename T>
T min3(T a, T b, T c)
{
	return min(min(a, b), c);
}

template<typename T>
T min4(T a, T b, T c, T d)
{
	return min3(a, b, min(c, d));
}

template<typename T>
T max3(T a, T b, T c)
{
	return max(max(a, b), c);
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

uint GetCubeFaceIndex(const float3 v)
{
	float3 vAbs = abs(v);
	uint faceIndex = 0;
	if(vAbs.z >= vAbs.x && vAbs.z >= vAbs.y)
	{
		faceIndex = v.z < 0 ? 5 : 4;
	}
	else if(vAbs.y >= vAbs.x)
	{
		faceIndex = v.y < 0 ? 3 : 2;
	}
	else
	{
		faceIndex = v.x < 0 ? 1 : 0;
	}
	return faceIndex;
}

//From "NEXT GENERATION POST PROCESSING IN CALL OF DUTY: ADVANCED WARFARE"
//http://advances.realtimerendering.com/s2014/index.html
float InterleavedGradientNoise(float2 uv)
{
	const float3 magic = float3(0.06711056f, 0.00583715f, 52.9829189f);
	return frac(magic.z * frac(dot(uv, magic.xy)));
}
float InterleavedGradientNoise(float2 uv, float offset)
{
	uv += offset * (float2(47, 17) * 0.695f);
	const float3 magic = float3( 0.06711056f, 0.00583715f, 52.9829189f );
	return frac(magic.z * frac(dot(uv, magic.xy)));
}

float ScreenFade(float2 uv)
{
	float2 fade = max(12.0f * abs(uv - 0.5f) - 5.0f, 0.0f);
	return saturate(1.0 - dot(fade, fade));
}

float Wireframe(float3 barycentrics, float thickness = 0.2f, float smoothing = 1.0f)
{
	float3 deltas = fwidth(barycentrics);
	float3 bary = smoothstep(deltas * thickness, deltas * (thickness + smoothing), barycentrics);
	float minBary = min(bary.x, min(bary.y, bary.z));
	return minBary;
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

// From keijiro: 3x3 Rotation matrix with an angle and an arbitrary vector
float3x3 AngleAxis3x3(float angle, float3 axis)
{
    float c, s;
    sincos(angle, s, c);

    float t = 1 - c;
    float x = axis.x;
    float y = axis.y;
    float z = axis.z;

    return float3x3(
        t * x * x + c,      t * x * y - s * z,  t * x * z + s * y,
        t * x * y + s * z,  t * y * y + c,      t * y * z - s * x,
        t * x * z - s * y,  t * y * z + s * x,  t * z * z + c
    );
}

float3x3 CreateTangentToWorld(float3 normal, float4 tangent)
{
	float3 T = tangent.xyz;
	float3 B = cross(normal, T) * tangent.w;
	float3x3 TBN = float3x3(T, B, normal);
	return TBN;
}

uint Flatten2D(uint2 index, uint2 dimensions)
{
	return index.x + index.y * dimensions.x;
}

uint Flatten3D(uint3 index, uint3 dimensions)
{
	return index.x + index.y * dimensions.x + index.z * dimensions.x * dimensions.y;
}

uint2 UnFlatten2D(uint index, uint2 dimensions)
{
	return uint2(index % dimensions.x, index / dimensions.x);
}

uint3 UnFlatten3D(uint index, uint3 dimensions)
{
	uint3 outIndex;
	outIndex.z = index / (dimensions.x * dimensions.y);
	index -= (outIndex.z * dimensions.x * dimensions.y);
	outIndex.y = index / dimensions.x;
	outIndex.x = index % dimensions.x;
	return outIndex;
}

float Max3(float3 v)
{
	return max(v.x, max(v.y, v.z));
}

float Min3(float3 v)
{
	return min(v.x, min(v.y, v.z));
}

uint DivideAndRoundUp(uint x, uint y)
{
	return (x + y - 1) / y;
}

bool RaySphereIntersect(float3 rayOrigin, float3 rayDirection, float3 sphereCenter, float sphereRadius, out float2 intersection)
{
    float3 oc = rayOrigin - sphereCenter;
    float b = dot(oc, rayDirection);
    float c = dot(oc, oc) - sphereRadius * sphereRadius;
    float h = b * b - c;
    if(h < 0.0)
	{
		intersection = -1.0f;
		return false;
	}
    h = sqrt(h);
    intersection = float2(-b - h, -b + h);
	return true;
}
