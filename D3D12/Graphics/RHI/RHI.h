#pragma once

enum class ResourceFormat
{
	Unknown,

	R8_UINT,
	R8_SINT,
	R8_UNORM,
	R8_SNORM,
	RG8_UINT,
	RG8_SINT,
	RG8_UNORM,
	RG8_SNORM,
	R16_UINT,
	R16_SINT,
	R16_UNORM,
	R16_SNORM,
	R16_FLOAT,
	BGRA4_UNORM,
	B5G6R5_UNORM,
	B5G5R5A1_UNORM,
	RGBA8_UINT,
	RGBA8_SINT,
	RGBA8_UNORM,
	RGBA8_SNORM,
	BGRA8_UNORM,
	RGBA8_UNORM_SRGB,
	BGRA8_UNORM_SRGB,
	RGB10A2_UNORM,
	R11G11B10_FLOAT,
	RG16_UINT,
	RG16_SINT,
	RG16_UNORM,
	RG16_SNORM,
	RG16_FLOAT,
	R32_UINT,
	R32_SINT,
	R32_FLOAT,
	RGBA16_UINT,
	RGBA16_SINT,
	RGBA16_FLOAT,
	RGBA16_UNORM,
	RGBA16_SNORM,
	RG32_UINT,
	RG32_SINT,
	RG32_FLOAT,
	RGB32_UINT,
	RGB32_SINT,
	RGB32_FLOAT,
	RGBA32_UINT,
	RGBA32_SINT,
	RGBA32_FLOAT,

	D16_UNORM,
	D24S8,
	X24G8_UINT,
	D32_FLOAT,
	D32S8,
	X32G8_UINT,

	BC1_UNORM,
	BC1_UNORM_SRGB,
	BC2_UNORM,
	BC2_UNORM_SRGB,
	BC3_UNORM,
	BC3_UNORM_SRGB,
	BC4_UNORM,
	BC4_SNORM,
	BC5_UNORM,
	BC5_SNORM,
	BC6H_UFLOAT,
	BC6H_SFLOAT,
	BC7_UNORM,
	BC7_UNORM_SRGB,

	Num,
};

enum class FormatType
{
	Integer,
	Normalized,
	Float,
	DepthStencil
};

struct FormatInfo
{
	ResourceFormat Format;
	const char* pName;
	uint8 BytesPerBlock;
	uint8 BlockSize;
	FormatType Type;
	bool RedChannel : 1;
	bool GreenChannel : 1;
	bool BlueChannel : 1;
	bool AlphaChannel : 1;
	bool IsDepth : 1;
	bool IsStencil : 1;
	bool IsSigned : 1;
	bool IsSRGB : 1;
	bool IsBC : 1;
};

const FormatInfo& GetFormatInfo(ResourceFormat format);
const uint32 GetFormatRowByteSize(ResourceFormat format, uint32 width);

inline constexpr ResourceFormat SRVFormatFromDepth(ResourceFormat format)
{
	switch (format)
	{
		// 32-bit Z w/ Stencil
	case ResourceFormat::D32S8:
	case ResourceFormat::X32G8_UINT:
		return ResourceFormat::R32_FLOAT;
		// No Stencil
	case ResourceFormat::D32_FLOAT:
	case ResourceFormat::R32_FLOAT:
		return ResourceFormat::R32_FLOAT;
		// 24-bit Z
	case ResourceFormat::D24S8:
	case ResourceFormat::X24G8_UINT:
		return ResourceFormat::D24S8;
		// 16-bit Z w/o Stencil
	case ResourceFormat::D16_UNORM:
	case ResourceFormat::R16_UNORM:
		return ResourceFormat::R16_UNORM;
	default:
		return format;
	}
}


inline constexpr ResourceFormat DSVFormat(ResourceFormat format)
{
	switch (format)
	{
	case ResourceFormat::R32_FLOAT:
		return ResourceFormat::D32_FLOAT;
	case ResourceFormat::R16_UNORM:
		return ResourceFormat::D16_UNORM;
	default:
		return format;
	}
}
