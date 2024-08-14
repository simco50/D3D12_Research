#pragma once

#include "CommonBindings.hlsli"
#include "Packing.hlsli"

struct Sphere
{
	float3 Position;
	float Radius;
};

struct AABB
{
	float4 Center;
	float4 Extents;
};

struct MaterialProperties
{
	float3 BaseColor;
	float3 Normal;
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

float3 ViewPositionFromDepth(float2 uv, float depth, float4x4 projectionInverse)
{
	float4 clip = float4(float2(uv.x, 1.0f - uv.y) * 2.0f - 1.0f, 0.0f, 1.0f) * cView.NearZ;
	float3 viewRay = mul(clip, cView.ClipToView).xyz;
	return viewRay * LinearizeDepth01(depth);
}

float3 ViewPositionFromDepth(float2 uv, Texture2D<float> depthTexture)
{
	float4 clip = float4(float2(uv.x, 1.0f - uv.y) * 2.0f - 1.0f, 0.0f, 1.0f) * cView.NearZ;
	float3 viewRay = mul(clip, cView.ClipToView).xyz;
	float depth = depthTexture.SampleLevel(sPointClamp, uv, 0);
	return viewRay * LinearizeDepth01(depth);
}

float3 WorldPositionFromDepth(float2 uv, float depth, float4x4 viewProjectionInverse)
{
	float4 clip = float4(float2(uv.x, 1.0f - uv.y) * 2.0f - 1.0f, depth, 1.0f);
	float4 world = mul(clip, viewProjectionInverse);
	return world.xyz / world.w;
}

enum class NormalReconstructMethod
{
	Taps3,
	Taps5,
	Taps13,
};

float3 ViewNormalFromDepth(float2 uv, Texture2D<float> depthTexture, NormalReconstructMethod method = NormalReconstructMethod::Taps3)
{
	SamplerState depthSampler = sPointClamp;
	float2 invDimensions = cView.ViewportDimensionsInv;
	float4x4 inverseProjection = cView.ClipToView;

	if(method == NormalReconstructMethod::Taps3)
	{
		float3 vpos0 = ViewPositionFromDepth(uv, depthTexture);
		float3 vpos1 = ViewPositionFromDepth(uv + float2(1, 0) * invDimensions, depthTexture);
		float3 vpos2 = ViewPositionFromDepth(uv + float2(0, -1) * invDimensions, depthTexture);
		return normalize(cross(vpos2 - vpos0, vpos1 - vpos0));
	}
	else if(method == NormalReconstructMethod::Taps5)
	{
		// János Turánszki' - Improved Normal Reconstruction
		// https://wickedengine.net/2019/09/22/improved-normal-reconstruction-from-depth/
		float3 vposc = ViewPositionFromDepth(uv, depthTexture);
		float3 vposl = ViewPositionFromDepth(uv + float2(-1, 0) * invDimensions, depthTexture);
		float3 vposr = ViewPositionFromDepth(uv + float2(1, 0) * invDimensions, depthTexture);
		float3 vposd = ViewPositionFromDepth(uv + float2(0, -1) * invDimensions, depthTexture);
		float3 vposu = ViewPositionFromDepth(uv + float2(0, 1) * invDimensions, depthTexture);

		float3 l = vposc - vposl;
		float3 r = vposr - vposc;
		float3 d = vposc - vposd;
		float3 u = vposu - vposc;

		float3 hDeriv = abs(l.z) < abs(r.z) ? l : r;
		float3 vDeriv = abs(d.z) < abs(u.z) ? d : u;

		return normalize(cross(hDeriv, vDeriv));
	}
	else if(method == NormalReconstructMethod::Taps13)
	{
		// Yuwen Wu - Accurate Normal Reconstruction
		// https://atyuwen.github.io/posts/normal-reconstruction/
		float depth_c = depthTexture.SampleLevel(depthSampler, uv, 0, int2( 0,  0));
		float depth_l = depthTexture.SampleLevel(depthSampler, uv, 0, int2(-1,  0));
		float depth_r = depthTexture.SampleLevel(depthSampler, uv, 0, int2( 1,  0));
		float depth_d = depthTexture.SampleLevel(depthSampler, uv, 0, int2( 0, -1));
		float depth_u = depthTexture.SampleLevel(depthSampler, uv, 0, int2( 0,  1));

		float3 posVS_c = ViewPositionFromDepth(uv, depth_c, cView.ClipToView);
		float3 posVS_l = ViewPositionFromDepth(uv, depth_l, cView.ClipToView);
		float3 posVS_r = ViewPositionFromDepth(uv, depth_r, cView.ClipToView);
		float3 posVS_d = ViewPositionFromDepth(uv, depth_d, cView.ClipToView);
		float3 posVS_u = ViewPositionFromDepth(uv, depth_u, cView.ClipToView);

		float3 l = posVS_c - posVS_l;
		float3 r = posVS_r - posVS_c;
		float3 d = posVS_c - posVS_d;
		float3 u = posVS_u - posVS_c;

		// get depth values at 1 & 2 pixels offsets from current along the horizontal axis
		float4 H = float4(
			depth_l,
			depth_r,
			depthTexture.SampleLevel(depthSampler, uv, 0, int2(-2, 0)),
			depthTexture.SampleLevel(depthSampler, uv, 0, int2( 2, 0))
		);

		// get depth values at 1 & 2 pixels offsets from current along the vertical axis
		float4 V = float4(
			depth_d,
			depth_u,
			depthTexture.SampleLevel(depthSampler, uv, 0, int2(0, -2)),
			depthTexture.SampleLevel(depthSampler, uv, 0, int2(0,  2))
		);

		// current pixel's depth difference from slope of offset depth samples
		// differs from original article because we're using non-linear depth values
		// see article's comments
		float2 he = abs((2 * H.xy - H.zw) - depth_c);
		float2 ve = abs((2 * V.xy - V.zw) - depth_c);

		// pick horizontal and vertical diff with the smallest depth difference from slopes
		float3 hDeriv = he.x < he.y ? l : r;
		float3 vDeriv = ve.x < ve.y ? d : u;

		// get view space normal from the cross product of the best derivatives
		return normalize(cross(hDeriv, vDeriv));
	}

	return 0;
}

// Convert screen space coordinates (0, width/height) to view space.
float3 ScreenToView(float4 screen, float2 screenDimensionsInv, float4x4 projectionInverse)
{
	// Convert to normalized texture coordinates
	float2 screenNormalized = screen.xy * screenDimensionsInv;
	return ViewPositionFromDepth(screenNormalized, screen.z, projectionInverse);
}

float2 ClipToUV(float2 clip)
{
	return clip * float2(0.5f, -0.5f) + 0.5f;
}

float2 UVToClip(float2 uv)
{
	return (uv - 0.5f) * float2(2.0f, -2.0f);
}

float2 TexelToUV(uint2 texel, float2 texelSize)
{
	return ((float2)texel + 0.5f) * texelSize;
}

float3 TexelToUV(uint3 texel, float3 texelSize)
{
	return ((float3)texel + 0.5f) * texelSize;
}

AABB AABBFromMinMax(float3 minimum, float3 maximum)
{
	AABB aabb;
	aabb.Center = float4((minimum + maximum) / 2.0f, 0);
	aabb.Extents = float4(maximum, 0) - aabb.Center;
	return aabb;
}

template<typename T>
T Square(T x)
{
	return x * x;
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
T Max(T a, T b)
{
	return max(a, b);
}
template<typename T>
T Max(T a, T b, T c)
{
	return max(a, max(b, c));
}
template<typename T>
T Max(T a, T b, T c, T d)
{
	return max(max(a, b), max(c, d));
}

template<typename T>
T Min(T a, T b)
{
	return min(a, b);
}
template<typename T>
T Min(T a, T b, T c)
{
	return min(a, min(b, c));
}
template<typename T>
T Min(T a, T b, T c, T d)
{
	return min(min(a, b), min(c, d));
}

float MaxComponent(float2 v)
{
	return max(v.x, v.y);
}
float MaxComponent(float3 v)
{
	return max(v.x, max(v.y, v.z));
}
float MaxComponent(float4 v)
{
	return max(max(v.x, v.y), max(v.z, v.w));
}

float MinComponent(float2 v)
{
	return min(v.x, v.y);
}
float MinComponent(float3 v)
{
	return min(v.x, min(v.y, v.z));
}
float MinComponent(float4 v)
{
	return min(min(v.x, v.y), min(v.z, v.w));
}

template<typename T, uint N>
uint ArraySize(T arr[N])
{
	return N;
}

template<typename T>
T LinearToSRGB(T linearRGB)
{
	return pow(linearRGB, 1.0f / 2.2f);
}

template<typename T>
T SRGBToLinear(T srgb)
{
	return pow(srgb, 2.2f);
}

float GetLuminance(float3 color)
{
	return dot(color, float3(0.2126729, 0.7151522, 0.0721750));
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

uint Flatten2D(uint2 index, uint dimensionsX)
{
	return index.x + index.y * dimensionsX;
}

uint Flatten3D(uint3 index, uint2 dimensionsXY)
{
	return index.x + index.y * dimensionsXY.x + index.z * dimensionsXY.x * dimensionsXY.y;
}

uint2 UnFlatten2D(uint index, uint dimensionsX)
{
	return uint2(index % dimensionsX, index / dimensionsX);
}

uint3 UnFlatten3D(uint index, uint2 dimensionsXY)
{
	uint3 outIndex;
	outIndex.z = index / (dimensionsXY.x * dimensionsXY.y);
	index -= (outIndex.z * dimensionsXY.x * dimensionsXY.y);
	outIndex.y = index / dimensionsXY.x;
	outIndex.x = index % dimensionsXY.x;
	return outIndex;
}

uint DivideAndRoundUp(uint x, uint y)
{
	return (x + y - 1) / y;
}

float3 HUE_2_RGB(float H)
{
	float R = abs(H * 6 - 3) - 1;
	float G = 2 - abs(H * 6 - 2);
	float B = 2 - abs(H * 6 - 4);
	return saturate(float3(R, G, B));
}

float3 HSV_2_RGB(in float3 HSV)
{
	float3 RGB = HUE_2_RGB(HSV.x);
	return ((RGB - 1) * HSV.y + 1) * HSV.z;
}