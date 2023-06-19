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
    
    { ResourceFormat::BC1_UNORM,         "BC1_UNORM",         8,   4, FormatType::Normalized,   3, false, false, false, false, true  },
    { ResourceFormat::BC2_UNORM,         "BC2_UNORM",         16,  4, FormatType::Normalized,   4, false, false, false, false, true  },
    { ResourceFormat::BC3_UNORM,         "BC3_UNORM",         16,  4, FormatType::Normalized,   4, false, false, false, false, true  },
    { ResourceFormat::BC4_UNORM,         "BC4_UNORM",         8,   4, FormatType::Normalized,   1, false, false, false, false, true  },
    { ResourceFormat::BC4_SNORM,         "BC4_SNORM",         8,   4, FormatType::Normalized,   1, false, false, false, false, true  },
    { ResourceFormat::BC5_UNORM,         "BC5_UNORM",         16,  4, FormatType::Normalized,   2, false, false, false, false, true  },
    { ResourceFormat::BC5_SNORM,         "BC5_SNORM",         16,  4, FormatType::Normalized,   2, false, false, false, false, true  },
    { ResourceFormat::BC6H_UFLOAT,       "BC6H_UFLOAT",       16,  4, FormatType::Float,        3, false, false, false, false, true  },
    { ResourceFormat::BC6H_SFLOAT,       "BC6H_SFLOAT",       16,  4, FormatType::Float,        3, false, false, true,  false, true  },
    { ResourceFormat::BC7_UNORM,         "BC7_UNORM",         16,  4, FormatType::Normalized,   4, false, false, false, false, true  },

	{ ResourceFormat::D16_UNORM,         "D16_UNORM",         2,   1, FormatType::DepthStencil, 1, true,  false, false, false, false },
	{ ResourceFormat::D32_FLOAT,         "D32",               4,   1, FormatType::DepthStencil, 1, true,  false, false, false, false },
	{ ResourceFormat::D24S8,             "D24S8",             4,   1, FormatType::DepthStencil, 1, true,  true,  false, false, false },
	{ ResourceFormat::D32S8,             "D32S8",             8,   1, FormatType::DepthStencil, 1, true,  true,  false, false, false },
};

static_assert(ARRAYSIZE(sFormatInfo) == (uint32)ResourceFormat::Num);

constexpr bool TestEnumValues()
{
	for (uint32 i = 0; i < ARRAYSIZE(sFormatInfo); ++i)
	{
		if (sFormatInfo[i].Format != (ResourceFormat)i)
			return false;
	}
	return true;
}
static_assert(TestEnumValues());

namespace RHI
{
	const FormatInfo& GetFormatInfo(ResourceFormat format)
	{
		const FormatInfo& info = sFormatInfo[(uint32)format];
		check(info.Format == format);
		return info;
	}

	const uint32 GetFormatByteSize(ResourceFormat format, uint32 width, uint32 height, uint32 depth)
	{
		const FormatInfo& info = GetFormatInfo(format);
		if (info.BlockSize > 0)
		{
			if (info.IsBC)
			{
				return Math::DivideAndRoundUp(width, 4) * height * depth * info.BytesPerBlock / info.BlockSize / info.BlockSize;
			}
			return width * height * depth * info.BytesPerBlock / info.BlockSize / info.BlockSize;
		}
		return 0;
	}

	uint64 GetRowPitch(ResourceFormat format, uint32 width, uint32 mipIndex)
	{
		const FormatInfo& info = GetFormatInfo(format);
		if (info.BlockSize > 0)
		{
			uint64 numBlocks = Math::Max(1u, Math::DivideAndRoundUp(width >> mipIndex, info.BlockSize));
			return numBlocks * info.BytesPerBlock;
		}
		return 0;
	}

	uint64 GetSlicePitch(ResourceFormat format, uint32 width, uint32 height, uint32 mipIndex)
	{
		const FormatInfo& info = GetFormatInfo(format);
		if (info.BlockSize > 0)
		{
			uint64 numBlocksX = Math::Max(1u, Math::DivideAndRoundUp(width >> mipIndex, info.BlockSize));
			uint64 numBlocksY = Math::Max(1u, Math::DivideAndRoundUp(height >> mipIndex, info.BlockSize));
			return numBlocksX * numBlocksY * info.BytesPerBlock;
		}
		return 0;
	}

	uint64 RHI::GetTextureMipByteSize(ResourceFormat format, uint32 width, uint32 height, uint32 depth, uint32 mipIndex)
	{
		return GetSlicePitch(format, width, height, mipIndex) * Math::Max(1u, depth >> mipIndex);
	}

	uint64 GetTextureByteSize(ResourceFormat format, uint32 width, uint32 height, uint32 depth, uint32 numMips)
	{
		uint64 size = 0;
		for (uint32 mipLevel = 0; mipLevel < numMips; ++mipLevel)
		{
			size += RHI:: GetTextureMipByteSize(format, width, height, depth, mipLevel);
		}
		return size;
	}
}
