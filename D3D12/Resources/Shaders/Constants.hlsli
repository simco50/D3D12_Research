#pragma once

static const float FLT_MIN  = 1.175494351e-38F;
static const float FLT_MAX  = 3.402823466e+38F;

static const float PI      	= 3.14159265358979323846;
static const float INV_PI   = 0.31830988618379067154;
static const float INV_2PI  = 0.15915494309189533577;
static const float INV_4PI  = 0.07957747154594766788;
static const float PI_DIV_2 = 1.57079632679489661923;
static const float PI_DIV_4 = 0.78539816339744830961;
static const float SQRT_2   = 1.41421356237309504880;

static const uint CLUSTERED_LIGHTING_MAX_LIGHTS_PER_CLUSTER = 128;
static const uint CLUSTERED_LIGHTING_NUM_BUCKETS = CLUSTERED_LIGHTING_MAX_LIGHTS_PER_CLUSTER / 32;

static const uint TILED_LIGHTING_TILE_SIZE = 8;
static const uint TILED_LIGHTING_MAX_LIGHTS_PER_TILE = 1024;
static const uint TILED_LIGHTING_NUM_BUCKETS = TILED_LIGHTING_MAX_LIGHTS_PER_TILE / 32;

namespace Colors
{
	static const float4 Red = float4(1, 0, 0, 1);
	static const float4 Green = float4(0, 1, 0, 1);
	static const float4 Blue = float4(0, 0, 1, 1);
	static const float4 Black = float4(0, 0, 0, 1);
	static const float4 White = float4(1, 1, 1, 1);
}

static const uint INVALID_HANDLE = 0xFFFFFFFF;

static const float3x3 IDENTITY_MATRIX_3 = float3x3(1, 0, 0, 0, 1, 0, 0, 0, 1);
static const float4x4 IDENTITY_MATRIX_4 = float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);

#define _INLINE_RT (_SM_MAJ >= 6 && _SM_MIN >= 5)

#if _PAYLOAD_QUALIFIERS
#define RAYPAYLOAD [raypayload]
#define RAYQUALIFIER(qualifiers) : qualifiers
#else
#define RAYPAYLOAD
#define RAYQUALIFIER(qualifiers)
#endif
