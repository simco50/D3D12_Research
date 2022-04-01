#pragma once

#define FLT_MIN         1.175494351e-38F
#define FLT_MAX         3.402823466e+38F

#define PI		  		3.14159265359
#define PI_DIV_2		1.57079632679
#define PI_DIV_4		0.78539816339
#define INV_PI	  		0.31830988618379067154

#define INVALID_HANDLE 0xFFFFFFFF

#define _INLINE_RT (_SM_MAJ >= 6 && _SM_MIN >= 5)

#if _PAYLOAD_QUALIFIERS
#define RAYPAYLOAD [raypayload]
#define RAYQUALIFIER(qualifiers) : qualifiers
#else
#define RAYPAYLOAD
#define RAYQUALIFIER(qualifiers)
#endif

#define IDENTITY_MATRIX_3 float3x3(1, 0, 0, 0, 1, 0, 0, 0, 1)
#define IDENTITY_MATRIX_4 float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)