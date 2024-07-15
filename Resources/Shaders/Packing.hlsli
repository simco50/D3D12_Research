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
