#pragma once

namespace Octahedral
{
	// Helpers for octahedron encoding of normals
	float2 OctWrap(float2 v)
	{
		return float2((1.0f - abs(v.y)) * (v.x >= 0.0f ? 1.0f : -1.0f), (1.0f - abs(v.x)) * (v.y >= 0.0f ? 1.0f : -1.0f));
	}

	float2 Pack(float3 n)
	{
		float2 p = float2(n.x, n.y) * (1.0f / (abs(n.x) + abs(n.y) + abs(n.z)));
		p = (n.z < 0.0f) ? OctWrap(p) : p;
		return p;
	}

	float3 Unpack(float2 p)
	{
		float3 n = float3(p.x, p.y, 1.0f - abs(p.x) - abs(p.y));
		float2 tmp = (n.z < 0.0f) ? OctWrap(float2(n.x, n.y)) : float2(n.x, n.y);
		n.x = tmp.x;
		n.y = tmp.y;
		return normalize(n);
	}
}

namespace RG16_FLOAT
{
	uint Pack(float2 value)
	{
		uint2 packed = f32tof16(value);
		return packed.x | (packed.y << 16u);
	}

	float2 Unpack(uint value)
	{
		return f16tof32(uint2(value & 0xFFFF, value >> 16));
	}
}

namespace RGBA16_FLOAT
{
	uint2 Pack(float4 value)
	{
		return uint2(
			RG16_FLOAT::Pack(value.xy), 
			RG16_FLOAT::Pack(value.zw)
		);
	}

	float4 Unpack(uint2 value)
	{
		return float4(
			RG16_FLOAT::Unpack(value.x), 
			RG16_FLOAT::Unpack(value.y)
		);
	}
}

namespace RGBA8_UNORM
{
	static const float Scale = 255.0f;
	static const float InvScale = 1.0f / Scale;

	uint Pack(float4 value)
	{
		uint4 packed = uint4(round(saturate(value) * Scale));
		return 
			((packed.x & 0xFF) << 0) | 
			((packed.y & 0xFF) << 8) | 
			((packed.z & 0xFF) << 16) | 
			((packed.w & 0xFF) << 24);
	}

	float4 Unpack(uint value)
	{
		uint4 v;
		v.x = (value << 24) >> 24;
		v.y = (value << 16) >> 24;
		v.z = (value << 8) >> 24;
		v.w = (value << 0) >> 24;
		return float4(v) * InvScale;
	}
}

namespace RGBA8_SNORM
{
	static const float Scale = 127.0f;
	static const float InvScale = 1.0f / Scale;

	uint Pack(float4 value)
	{
		int4 signedV = round(clamp(value, -1.0, 1.0) * Scale);
		return 
			((signedV.x & 0xFF) << 0u) | 
			((signedV.y & 0xFF) << 8u) | 
			((signedV.z & 0xFF) << 16u) | 
			((signedV.w & 0xFF) << 24u);
	}

	float4 Unpack(uint value)
	{
		int4 signedV;
		signedV.x = (int)(value << 24) >> 24;
		signedV.y = (int)(value << 16) >> 24;
		signedV.z = (int)(value << 8) >> 24;
		signedV.w = (int)(value << 0) >> 24;
		return float4(signedV) * InvScale;
	}
}

namespace RG16_UNORM
{
	static const float Scale = 65535.0f;
	static const float InvScale = 1.0f / Scale;

	uint Pack(float2 value)
	{
		uint2 v = round(saturate(value) * Scale);
		return 
			((v.x & 0xFFFF) << 0u) | 
			((v.y & 0xFFFF) << 16u);
	}

	float2 Unpack(uint value)
	{
		uint2 v;
		v.x = (value << 16) >> 16;
		v.y = (value << 0) >> 16;
		return float2(v) * InvScale;
	}
}

namespace RGBA16_UNORM
{
	uint2 Pack(float4 value)
	{
		return uint2(
			RG16_UNORM::Pack(value.xy),
			RG16_UNORM::Pack(value.zw)
		);
	}

	float4 Unpack(uint2 value)
	{
		return float4(
			RG16_UNORM::Unpack(value.x), 
			RG16_UNORM::Unpack(value.y)
		);
	}
}

namespace RG16_SNORM
{
	static const float Scale = 32767.0f;
	static const float InvScale = 1.0f / Scale;

	uint Pack(float2 value)
	{
		int2 signedV = round(clamp(value, -1.0f, 1.0f) * Scale);
		return 
			((signedV.x & 0xFFFF) << 0u) | 
			((signedV.y & 0xFFFF) << 16u);
	}

	float2 Unpack(uint value)
	{
		int2 signedV;
		signedV.x = (int)(value << 16) >> 16;
		signedV.y = (int)(value << 0) >> 16;
		return float2(signedV) * InvScale;
	}
}

namespace RGBA16_SNORM
{
	uint2 Pack(float4 value)
	{
		return uint2(
			RG16_SNORM::Pack(value.xy),
			RG16_SNORM::Pack(value.zw)
		);
	}

	float4 Unpack(uint2 value)
	{
		return float4(
			RG16_SNORM::Unpack(value.x), 
			RG16_SNORM::Unpack(value.y)
		);
	}
}

namespace R11G11B10_FLOAT
{
	uint Pack(float3 value)
	{
		return
			((f32tof16(value.x) << 17) & 0xFFE00000) |
			((f32tof16(value.y) << 6 ) & 0x001FFC00) |
			((f32tof16(value.z) >> 5 ) & 0x000003FF);
	}

	float3 Unpack(uint value)
	{
		return float3(
			f16tof32((value >> 17) & 0x7FF0),
			f16tof32((value >> 6 ) & 0x7FF0),
			f16tof32((value << 5 ) & 0x7FE0));
	}
}

namespace RGB10A2_SNORM
{
	static const float Scale = 511.0f;
	static const float InvScale = 1.0f / Scale;

	uint Pack(float4 value)
	{
		return
			((int)(round(clamp(value.x, -1.0f, 1.0f) * Scale)) & 0x3FF) << 0 |
			((int)(round(clamp(value.y, -1.0f, 1.0f) * Scale)) & 0x3FF) << 10 |
			((int)(round(clamp(value.z, -1.0f, 1.0f) * Scale)) & 0x3FF) << 20 |
			((int)(round(clamp(value.w, -1.0f, 1.0f))) << 30);
	}

	float4 Unpack(uint value)
	{
		int sValue = int(value);
		return float4(
			((sValue << 22) >> 22) * InvScale,
			((sValue << 12) >> 22) * InvScale,
			((sValue << 2) >> 22) * InvScale,
			((sValue << 0) >> 30));
	}
}

namespace RGB10A2_UNORM
{
	static const float Scale = 1023.0f;
	static const float InvScale = 1.0f / Scale;
	static const float AlphaScale = 3.0f;
	static const float InvAlphaScale = 1.0f / AlphaScale;

	uint Pack(float4 value)
	{
		return
			((int)(round(saturate(value.x) * Scale)) & 0x3FF) << 0 |
			((int)(round(saturate(value.y) * Scale)) & 0x3FF) << 10 |
			((int)(round(saturate(value.z) * Scale)) & 0x3FF) << 20 |
			((int)(round(saturate(value.w) * AlphaScale)) << 30);
	}

	float4 Unpack(uint value)
	{
		return float4(
			((value << 22) >> 22) * InvScale,
			((value << 12) >> 22) * InvScale,
			((value << 2) >> 22) * InvScale,
			((value << 0) >> 30) * InvAlphaScale
		);
	}
}


// From https://github.com/microsoft/DirectX-Graphics-Samples/blob/master/MiniEngine/Core/Shaders/PixelPacking_RGBE.hlsli
namespace R9G9B9E5_SHAREDEXP
{
	uint Pack(float3 value)
	{
		// To determine the shared exponent, we must clamp the channels to an expressible range
		const float kMaxVal = asfloat(0x477F8000); // 1.FF x 2^+15
		const float kMinVal = asfloat(0x37800000); // 1.00 x 2^-16

		// Non-negative and <= kMaxVal
		value = clamp(value, 0, kMaxVal);

		// From the maximum channel we will determine the exponent.  We clamp to a min value
		// so that the exponent is within the valid 5-bit range.
		float MaxChannel = max(max(kMinVal, value.r), max(value.g, value.b));

		// 'Bias' has to have the biggest exponent plus 15 (and nothing in the mantissa).  When
		// added to the three channels, it shifts the explicit '1' and the 8 most significant
		// mantissa bits into the low 9 bits.  IEEE rules of float addition will round rather
		// than truncate the discarded bits.  Channels with smaller natural exponents will be
		// shifted further to the right (discarding more bits).
		float Bias = asfloat((asuint(MaxChannel) + 0x07804000) & 0x7F800000);

		// Shift bits into the right places
		uint3 RGB = asuint(value + Bias);
		uint E = (asuint(Bias) << 4) + 0x10000000;
		return E | RGB.b << 18 | RGB.g << 9 | (RGB.r & 0x1FF);
	}

	float3 Unpack(uint v)
	{
		float3 rgb = uint3(v, v >> 9, v >> 18) & 0x1FF;
		return ldexp(rgb, (int)(v >> 27) - 24);
	}
}
