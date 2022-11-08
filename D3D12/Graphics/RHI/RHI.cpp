#include "stdafx.h"
#include "RHI.h"

static constexpr FormatInfo sFormatInfo[] = {
    // Format							 pName             Bytes BlockSize   Type        Components Depth  Stencl Signed SRGB   IsBC
    { ResourceFormat::Unknown,           "UNKNOWN",           0,   0, FormatType::Integer,      0, false, false, false, false, false },
    { ResourceFormat::R8_UINT,           "R8_UINT",           1,   1, FormatType::Integer,      1, false, false, false, false, false },
    { ResourceFormat::R8_SINT,           "R8_SINT",           1,   1, FormatType::Integer,      1, false, false, true,  false, false },
    { ResourceFormat::R8_UNORM,          "R8_UNORM",          1,   1, FormatType::Normalized,   1, false, false, false, false, false },
    { ResourceFormat::R8_SNORM,          "R8_SNORM",          1,   1, FormatType::Normalized,   1, false, false, false, false, false },
    { ResourceFormat::RG8_UINT,          "RG8_UINT",          2,   1, FormatType::Integer,      2, false, false, false, false, false },
    { ResourceFormat::RG8_SINT,          "RG8_SINT",          2,   1, FormatType::Integer,      2, false, false, true,  false, false },
    { ResourceFormat::RG8_UNORM,         "RG8_UNORM",         2,   1, FormatType::Normalized,   2, false, false, false, false, false },
    { ResourceFormat::RG8_SNORM,         "RG8_SNORM",         2,   1, FormatType::Normalized,   2, false, false, false, false, false },
    { ResourceFormat::R16_UINT,          "R16_UINT",          2,   1, FormatType::Integer,      1, false, false, false, false, false },
    { ResourceFormat::R16_SINT,          "R16_SINT",          2,   1, FormatType::Integer,      1, false, false, true,  false, false },
    { ResourceFormat::R16_UNORM,         "R16_UNORM",         2,   1, FormatType::Normalized,   1, false, false, false, false, false },
    { ResourceFormat::R16_SNORM,         "R16_SNORM",         2,   1, FormatType::Normalized,   1, false, false, false, false, false },
    { ResourceFormat::R16_FLOAT,         "R16_FLOAT",         2,   1, FormatType::Float,        1, false, false, true,  false, false },
    { ResourceFormat::BGRA4_UNORM,       "BGRA4_UNORM",       2,   1, FormatType::Normalized,   4, false, false, false, false, false },
    { ResourceFormat::B5G6R5_UNORM,      "B5G6R5_UNORM",      2,   1, FormatType::Normalized,   3, false, false, false, false, false },
    { ResourceFormat::B5G5R5A1_UNORM,    "B5G5R5A1_UNORM",    2,   1, FormatType::Normalized,   4, false, false, false, false, false },
    { ResourceFormat::RGBA8_UINT,        "RGBA8_UINT",        4,   1, FormatType::Integer,      4, false, false, false, false, false },
    { ResourceFormat::RGBA8_SINT,        "RGBA8_SINT",        4,   1, FormatType::Integer,      4, false, false, true,  false, false },
    { ResourceFormat::RGBA8_UNORM,       "RGBA8_UNORM",       4,   1, FormatType::Normalized,   4, false, false, false, false, false },
    { ResourceFormat::RGBA8_SNORM,		 "RGBA8_SNORM",       4,   1, FormatType::Normalized,   4, false, false, false, false, false },
    { ResourceFormat::BGRA8_UNORM,       "BGRA8_UNORM",       4,   1, FormatType::Normalized,   4, false, false, false, false, false },
    { ResourceFormat::RGBA8_UNORM_SRGB,  "SRGBA8_UNORM_SRGB", 4,   1, FormatType::Normalized,   4, false, false, false, true , false },
    { ResourceFormat::BGRA8_UNORM_SRGB,  "SBGRA8_UNORM_SRGB", 4,   1, FormatType::Normalized,   4, false, false, false, false, false },
    { ResourceFormat::RGB10A2_UNORM,	 "R10GBA2_UNORM", 	  4,   1, FormatType::Normalized,   4, false, false, false, false, false },
    { ResourceFormat::R11G11B10_FLOAT,   "R11G11B10_FLOAT",   4,   1, FormatType::Float,        3, false, false, false, false, false },
    { ResourceFormat::RG16_UINT,         "RG16_UINT",         4,   1, FormatType::Integer,      2, false, false, false, false, false },
    { ResourceFormat::RG16_SINT,         "RG16_SINT",         4,   1, FormatType::Integer,      2, false, false, true,  false, false },
    { ResourceFormat::RG16_UNORM,        "RG16_UNORM",        4,   1, FormatType::Normalized,   2, false, false, false, false, false },
    { ResourceFormat::RG16_SNORM,        "RG16_SNORM",        4,   1, FormatType::Normalized,   2, false, false, false, false, false },
    { ResourceFormat::RG16_FLOAT,        "RG16_FLOAT",        4,   1, FormatType::Float,        2, false, false, true,  false, false },
    { ResourceFormat::R32_UINT,          "R32_UINT",          4,   1, FormatType::Integer,      1, false, false, false, false, false },
    { ResourceFormat::R32_SINT,          "R32_SINT",          4,   1, FormatType::Integer,      1, false, false, true,  false, false },
    { ResourceFormat::R32_FLOAT,         "R32_FLOAT",         4,   1, FormatType::Float,        1, false, false, true,  false, false },
    { ResourceFormat::RGBA16_UINT,       "RGBA16_UINT",       8,   1, FormatType::Integer,      4, false, false, false, false, false },
    { ResourceFormat::RGBA16_SINT,       "RGBA16_SINT",       8,   1, FormatType::Integer,      4, false, false, true,  false, false },
    { ResourceFormat::RGBA16_FLOAT,      "RGBA16_FLOAT",      8,   1, FormatType::Float,        4, false, false, true,  false, false },
    { ResourceFormat::RGBA16_UNORM,      "RGBA16_UNORM",      8,   1, FormatType::Normalized,   4, false, false, false, false, false },
    { ResourceFormat::RGBA16_SNORM,      "RGBA16_SNORM",      8,   1, FormatType::Normalized,   4, false, false, false, false, false },
    { ResourceFormat::RG32_UINT,         "RG32_UINT",         8,   1, FormatType::Integer,      2, false, false, false, false, false },
    { ResourceFormat::RG32_SINT,         "RG32_SINT",         8,   1, FormatType::Integer,      2, false, false, true,  false, false },
    { ResourceFormat::RG32_FLOAT,        "RG32_FLOAT",        8,   1, FormatType::Float,        2, false, false, true,  false, false },
    { ResourceFormat::RGB32_UINT,        "RGB32_UINT",        12,  1, FormatType::Integer,      3, false, false, false, false, false },
    { ResourceFormat::RGB32_SINT,        "RGB32_SINT",        12,  1, FormatType::Integer,      3, false, false, true,  false, false },
    { ResourceFormat::RGB32_FLOAT,       "RGB32_FLOAT",       12,  1, FormatType::Float,        3, false, false, true,  false, false },
    { ResourceFormat::RGBA32_UINT,       "RGBA32_UINT",       16,  1, FormatType::Integer,      4, false, false, false, false, false },
    { ResourceFormat::RGBA32_SINT,       "RGBA32_SINT",       16,  1, FormatType::Integer,      4, false, false, true,  false, false },
    { ResourceFormat::RGBA32_FLOAT,      "RGBA32_FLOAT",      16,  1, FormatType::Float,        4, false, false, true,  false, false },
    { ResourceFormat::D16_UNORM,         "D16_UNORM",         2,   1, FormatType::DepthStencil, 1, true,  false, false, false, false },
    { ResourceFormat::D24S8,             "D24S8",             4,   1, FormatType::DepthStencil, 1, true,  true,  false, false, false },
    { ResourceFormat::X24G8_UINT,        "X24G8_UINT",        4,   1, FormatType::Integer,      1, false, true,  false, false, false },
    { ResourceFormat::D32_FLOAT,         "D32",               4,   1, FormatType::DepthStencil, 1, true,  false, false, false, false },
    { ResourceFormat::D32S8,             "D32S8",             8,   1, FormatType::DepthStencil, 1, true,  true,  false, false, false },
    { ResourceFormat::X32G8_UINT,        "X32G8_UINT",        8,   1, FormatType::Integer,      1, false, true,  false, false, false },
    { ResourceFormat::BC1_UNORM,         "BC1_UNORM",         8,   4, FormatType::Normalized,   3, false, false, false, false, true  },
    { ResourceFormat::BC1_UNORM_SRGB,    "BC1_UNORM_SRGB",    8,   4, FormatType::Normalized,   3, false, false, false, true,  true  },
    { ResourceFormat::BC2_UNORM,         "BC2_UNORM",         16,  4, FormatType::Normalized,   4, false, false, false, false, true  },
    { ResourceFormat::BC2_UNORM_SRGB,    "BC2_UNORM_SRGB",    16,  4, FormatType::Normalized,   4, false, false, false, true,  true  },
    { ResourceFormat::BC3_UNORM,         "BC3_UNORM",         16,  4, FormatType::Normalized,   4, false, false, false, false, true  },
    { ResourceFormat::BC3_UNORM_SRGB,    "BC3_UNORM_SRGB",    16,  4, FormatType::Normalized,   4, false, false, false, true,  true  },
    { ResourceFormat::BC4_UNORM,         "BC4_UNORM",         8,   4, FormatType::Normalized,   1, false, false, false, false, true  },
    { ResourceFormat::BC4_SNORM,         "BC4_SNORM",         8,   4, FormatType::Normalized,   1, false, false, false, false, true  },
    { ResourceFormat::BC5_UNORM,         "BC5_UNORM",         16,  4, FormatType::Normalized,   2, false, false, false, false, true  },
    { ResourceFormat::BC5_SNORM,         "BC5_SNORM",         16,  4, FormatType::Normalized,   2, false, false, false, false, true  },
    { ResourceFormat::BC6H_UFLOAT,       "BC6H_UFLOAT",       16,  4, FormatType::Float,        3, false, false, false, false, true  },
    { ResourceFormat::BC6H_SFLOAT,       "BC6H_SFLOAT",       16,  4, FormatType::Float,        3, false, false, true,  false, true  },
    { ResourceFormat::BC7_UNORM,         "BC7_UNORM",         16,  4, FormatType::Normalized,   4, false, false, false, false, true  },
    { ResourceFormat::BC7_UNORM_SRGB,    "BC7_UNORM_SRGB",    16,  4, FormatType::Normalized,   4, false, false, false, true,  true  },
};

static_assert(ARRAYSIZE(sFormatInfo) == (uint32)ResourceFormat::Num);

const FormatInfo& GetFormatInfo(ResourceFormat format)
{
	const FormatInfo& info = sFormatInfo[(uint32)format];
	check(info.Format == format);
	return info;
}

const uint32 GetFormatByteSize(ResourceFormat format, uint32 width, uint32 height, uint32 depth)
{
	const FormatInfo& info = GetFormatInfo(format);
	if(info.BlockSize > 0)
		return (width / info.BlockSize) * (height / info.BlockSize) * depth * info.BytesPerBlock;
	return 0;
}

ResourceFormat SRVFormatFromDepth(ResourceFormat format)
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

ResourceFormat DSVFormat(ResourceFormat format)
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
