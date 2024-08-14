#include "stdafx.h"
#include "RHI.h"

#define FORMAT_TYPE(name) #name, ResourceFormat::name

static constexpr FormatInfo sFormatInfo[] = {
    // Format							Type					Bytes	BlockSize  Components	Depth  Stencl Signed IsBC
    { FORMAT_TYPE(Unknown),				FormatType::Integer,      0,		0,		0,			false, false, false, false },
    { FORMAT_TYPE(R8_UINT),				FormatType::Integer,      1,		1,		1,			false, false, false, false },
    { FORMAT_TYPE(R8_SINT),				FormatType::Integer,      1,		1,		1,			false, false, true,  false },
    { FORMAT_TYPE(R8_UNORM),			FormatType::Normalized,   1,		1,		1,			false, false, false, false },
    { FORMAT_TYPE(R8_SNORM),			FormatType::Normalized,   1,		1,		1,			false, false, false, false },
    { FORMAT_TYPE(RG8_UINT),			FormatType::Integer,      2,		1,		2,			false, false, false, false },
    { FORMAT_TYPE(RG8_SINT),			FormatType::Integer,      2,		1,		2,			false, false, true,  false },
    { FORMAT_TYPE(RG8_UNORM),			FormatType::Normalized,   2,		1,		2,			false, false, false, false },
    { FORMAT_TYPE(RG8_SNORM),			FormatType::Normalized,   2,		1,		2,			false, false, false, false },
    { FORMAT_TYPE(R16_UINT),			FormatType::Integer,      2,		1,		1,			false, false, false, false },
    { FORMAT_TYPE(R16_SINT),			FormatType::Integer,      2,		1,		1,			false, false, true,  false },
    { FORMAT_TYPE(R16_UNORM),			FormatType::Normalized,   2,		1,		1,			false, false, false, false },
    { FORMAT_TYPE(R16_SNORM),			FormatType::Normalized,   2,		1,		1,			false, false, false, false },
    { FORMAT_TYPE(R16_FLOAT),			FormatType::Float,        2,		1,		1,			false, false, true,  false },
    { FORMAT_TYPE(BGRA4_UNORM),			FormatType::Normalized,   2,		1,		4,			false, false, false, false },
    { FORMAT_TYPE(B5G6R5_UNORM),		FormatType::Normalized,   2,		1,		3,			false, false, false, false },
    { FORMAT_TYPE(B5G5R5A1_UNORM),		FormatType::Normalized,   2,		1,		4,			false, false, false, false },
    { FORMAT_TYPE(RGBA8_UINT),			FormatType::Integer,      4,		1,		4,			false, false, false, false },
    { FORMAT_TYPE(RGBA8_SINT),			FormatType::Integer,      4,		1,		4,			false, false, true,  false },
    { FORMAT_TYPE(RGBA8_UNORM),			FormatType::Normalized,   4,		1,		4,			false, false, false, false },
    { FORMAT_TYPE(RGBA8_SNORM),			FormatType::Normalized,   4,		1,		4,			false, false, false, false },
    { FORMAT_TYPE(BGRA8_UNORM),			FormatType::Normalized,   4,		1,		4,			false, false, false, false },
    { FORMAT_TYPE(RGB10A2_UNORM),		FormatType::Normalized,   4,		1,		4,			false, false, false, false },
    { FORMAT_TYPE(R11G11B10_FLOAT),		FormatType::Float,        4,		1,		3,			false, false, false, false },
    { FORMAT_TYPE(RG16_UINT),			FormatType::Integer,      4,		1,		2,			false, false, false, false },
    { FORMAT_TYPE(RG16_SINT),			FormatType::Integer,      4,		1,		2,			false, false, true,  false },
    { FORMAT_TYPE(RG16_UNORM),			FormatType::Normalized,   4,		1,		2,			false, false, false, false },
    { FORMAT_TYPE(RG16_SNORM),			FormatType::Normalized,   4,		1,		2,			false, false, false, false },
    { FORMAT_TYPE(RG16_FLOAT),			FormatType::Float,        4,		1,		2,			false, false, true,  false },
    { FORMAT_TYPE(R32_UINT),			FormatType::Integer,      4,		1,		1,			false, false, false, false },
    { FORMAT_TYPE(R32_SINT),			FormatType::Integer,      4,		1,		1,			false, false, true,  false },
    { FORMAT_TYPE(R32_FLOAT),			FormatType::Float,        4,		1,		1,			false, false, true,  false },
    { FORMAT_TYPE(RGBA16_UINT),			FormatType::Integer,      8,		1,		4,			false, false, false, false },
    { FORMAT_TYPE(RGBA16_SINT),			FormatType::Integer,      8,		1,		4,			false, false, true,  false },
    { FORMAT_TYPE(RGBA16_FLOAT),		FormatType::Float,        8,		1,		4,			false, false, true,  false },
    { FORMAT_TYPE(RGBA16_UNORM),		FormatType::Normalized,   8,		1,		4,			false, false, false, false },
    { FORMAT_TYPE(RGBA16_SNORM),		FormatType::Normalized,   8,		1,		4,			false, false, false, false },
    { FORMAT_TYPE(RG32_UINT),			FormatType::Integer,      8,		1,		2,			false, false, false, false },
    { FORMAT_TYPE(RG32_SINT),			FormatType::Integer,      8,		1,		2,			false, false, true,  false },
    { FORMAT_TYPE(RG32_FLOAT),			FormatType::Float,        8,		1,		2,			false, false, true,  false },
    { FORMAT_TYPE(RGB32_UINT),			FormatType::Integer,      12,		1,		3,			false, false, false, false },
    { FORMAT_TYPE(RGB32_SINT),			FormatType::Integer,      12,		1,		3,			false, false, true,  false },
    { FORMAT_TYPE(RGB32_FLOAT),			FormatType::Float,        12,		1,		3,			false, false, true,  false },
    { FORMAT_TYPE(RGBA32_UINT),			FormatType::Integer,      16,		1,		4,			false, false, false, false },
    { FORMAT_TYPE(RGBA32_SINT),			FormatType::Integer,      16,		1,		4,			false, false, true,  false },
	{ FORMAT_TYPE(RGBA32_FLOAT),		FormatType::Float,        16,		1,		4,			false, false, true,  false },
	{ FORMAT_TYPE(R9G9B9E5_SHAREDEXP),	FormatType::Float,        4,		1,		3,			false, false, true,  false },
    { FORMAT_TYPE(BC1_UNORM),			FormatType::Normalized,   8,		4,		3,			false, false, false, true  },
    { FORMAT_TYPE(BC2_UNORM),			FormatType::Normalized,   16,		4,		4,			false, false, false, true  },
    { FORMAT_TYPE(BC3_UNORM),			FormatType::Normalized,   16,		4,		4,			false, false, false, true  },
    { FORMAT_TYPE(BC4_UNORM),			FormatType::Normalized,   8,		4,		1,			false, false, false, true  },
    { FORMAT_TYPE(BC4_SNORM),			FormatType::Normalized,   8,		4,		1,			false, false, false, true  },
    { FORMAT_TYPE(BC5_UNORM),			FormatType::Normalized,   16,		4,		2,			false, false, false, true  },
    { FORMAT_TYPE(BC5_SNORM),			FormatType::Normalized,   16,		4,		2,			false, false, false, true  },
    { FORMAT_TYPE(BC6H_UFLOAT),			FormatType::Float,        16,		4,		3,			false, false, false, true  },
    { FORMAT_TYPE(BC6H_SFLOAT),			FormatType::Float,        16,		4,		3,			false, false, true,  true  },
    { FORMAT_TYPE(BC7_UNORM),			FormatType::Normalized,   16,		4,		4,			false, false, false, true  },
	{ FORMAT_TYPE(D16_UNORM),			FormatType::DepthStencil, 2,		1,		1,			true,  false, false, false },
	{ FORMAT_TYPE(D32_FLOAT),			FormatType::DepthStencil, 4,		1,		1,			true,  false, false, false },
	{ FORMAT_TYPE(D24S8),				FormatType::DepthStencil, 4,		1,		1,			true,  true,  false, false },
	{ FORMAT_TYPE(D32S8),				FormatType::DepthStencil, 8,		1,		1,			true,  true,  false, false },
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
		gAssert(info.Format == format);
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
