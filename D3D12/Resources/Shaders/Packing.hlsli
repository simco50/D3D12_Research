#pragma once

// Helpers for octahedron encoding of normals
float2 OctWrap(float2 v)
{
	return float2((1.0f - abs(v.y)) * (v.x >= 0.0f ? 1.0f : -1.0f), (1.0f - abs(v.x)) * (v.y >= 0.0f ? 1.0f : -1.0f));
}

float2 EncodeNormalOctahedron(float3 n)
{
	float2 p = float2(n.x, n.y) * (1.0f / (abs(n.x) + abs(n.y) + abs(n.z)));
	p = (n.z < 0.0f) ? OctWrap(p) : p;
	return p;
}

float3 DecodeNormalOctahedron(float2 p)
{
	float3 n = float3(p.x, p.y, 1.0f - abs(p.x) - abs(p.y));
	float2 tmp = (n.z < 0.0f) ? OctWrap(float2(n.x, n.y)) : float2(n.x, n.y);
	n.x = tmp.x;
	n.y = tmp.y;
	return normalize(n);
}

uint Pack_RG16_FLOAT(float2 value)
{
	uint2 packed = f32tof16(value);
	return packed.x | (packed.y << 16u);
}

float2 Unpack_RG16_FLOAT(uint value)
{
	return f16tof32(uint2(value & 0xFFFF, value >> 16));
}

uint2 Pack_RGBA16_FLOAT(float4 value)
{
	uint4 packed = f32tof16(value);
	return uint2(packed.x | (packed.y << 16u), packed.z | (packed.w << 16u));
}

float4 Unpack_RGBA16_FLOAT(uint2 value)
{
	return float4(
		Unpack_RG16_FLOAT(value.x), 
		Unpack_RG16_FLOAT(value.y)
	);
}

uint Pack_RGBA8_UNORM(float4 value)
{
	uint4 packed = uint4(round(saturate(value) * 255.0));
    return packed.x | (packed.y << 8) | (packed.z << 16) | (packed.w << 24);
}

float4 Unpack_RGBA8_UNORM(uint value)
{
    uint4 packed = uint4(value & 0xff, (value >> 8) & 0xff, (value >> 16) & 0xff, value >> 24);
    return float4(packed) / 255.0;
}

uint Pack_RGBA8_SNORM(float4 value)
{
    int4 packed = int4(round(clamp(value, -1.0, 1.0) * 127.0)) & 0xff;
    return uint(packed.x | (packed.y << 8) | (packed.z << 16) | (packed.w << 24));
}

float4 Unpack_RGBA8_SNORM(uint value)
{
    int sValue = int(value);
    int4 packed = int4(sValue << 24, sValue << 16, sValue << 8, sValue) >> 24;
    return clamp(float4(packed) / 127.0, -1.0, 1.0);
}

float2 Unpack_RG16_SNORM(uint value)
{
	int2 signedV;
	signedV.x = (int)(value << 16) >> 16;
	signedV.y = (int)value >> 16;
	return max(float2(signedV) / 32767.0f, -1.0f);
}

float4 Unpack_RGBA16_SNORM(uint2 value)
{
	return float4(
		Unpack_RG16_SNORM(value.x), 
		Unpack_RG16_SNORM(value.y)
	);
}

uint Pack_R11G11B10_FLOAT(float3 rgb)
{
	uint r = (f32tof16(rgb.x) << 17) & 0xFFE00000;
	uint g = (f32tof16(rgb.y) << 6) & 0x001FFC00;
	uint b = (f32tof16(rgb.z) >> 5) & 0x000003FF;
	return r | g | b;
}

float3 Unpack_R11G11B10_FLOAT(uint rgb)
{
	float r = f16tof32((rgb >> 17) & 0x7FF0);
	float g = f16tof32((rgb >> 6) & 0x7FF0);
	float b = f16tof32((rgb << 5) & 0x7FE0);
	return float3(r, g, b);
}

float4 Unpack_RGB10A2_SNORM(uint value)
{
	const float scaleXYZ = 1.0f / 511.0f;
	int sValue = int(value);
	return float4(
		((sValue << 22) >> 22) * scaleXYZ,
		((sValue << 12) >> 22) * scaleXYZ,
		((sValue << 2) >> 22) * scaleXYZ,
		((sValue << 0) >> 30) * 1.0f
	);
}