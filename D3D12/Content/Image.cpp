#include "stdafx.h"
#include "Image.h"

//#define STBI_NO_PSD
#define STBI_NO_GIF
#define STBI_NO_PIC
#define STBI_NO_PNM

#include "Core/Paths.h"
#include "Core/Stream.h"

#include <External/Stb/stb_image.h>
#include <External/Stb/stb_image_write.h>

Image::Image(ResourceFormat format)
	: m_Format(format)
{
}

Image::Image(uint32 width, uint32 height, uint32 depth, ResourceFormat format, uint32 mips, const void* pInitialData /*= nullptr*/)
	: m_Format(format)
{
	SetSize(width, height, depth, mips);
	if (pInitialData)
	{
		SetData(pInitialData);
	}
}

bool Image::Load(const char* inputStream)
{
	const String extension = Paths::GetFileExtenstion(inputStream);
	bool success = false;

	FileStream stream;
	if (!stream.Open(inputStream, FileMode::Read))
		return false;

	if (extension == "dds")
	{
		success = LoadDDS(stream);
	}
	//If not one of the above, load with Stbi by default (jpg, png, tga, bmp, ...)
	else
	{
		success = LoadSTB(stream);
	}
	return success;
}

bool Image::Load(Stream& stream, const char* pFormatHint)
{
	if (String(pFormatHint).find("dds") != String::npos)
	{
		return LoadDDS(stream);
	}
	return LoadSTB(stream);
}

bool Image::SetSize(uint32 width, uint32 height, uint32 depth, uint32 numMips)
{
	m_Width = Math::Max(1u, width);
	m_Height = Math::Max(1u, height);
	m_Depth = Math::Max(1u, depth);
	m_MipLevels = numMips;
	m_Pixels.resize(RHI::GetTextureByteSize(m_Format, m_Width, m_Height, m_Depth, numMips));
	return true;
}

bool Image::SetData(const void* pPixels)
{
	return SetData(pPixels, 0, (uint32)m_Pixels.size());
}

bool Image::SetData(const void* pData, uint32 offsetInBytes, uint32 sizeInBytes)
{
	check(offsetInBytes + sizeInBytes <= m_Pixels.size());
	memcpy(m_Pixels.data() + offsetInBytes, pData, sizeInBytes);
	return true;
}

bool Image::SetPixel(uint32 x, uint32 y, const Color& color)
{
	const FormatInfo& info = RHI::GetFormatInfo(m_Format);
	check(!info.IsBC, "Can't get pixel data from block compressed texture");
	if (x + y * m_Width >= (uint32)m_Pixels.size())
	{
		return false;
	}
	unsigned char* pPixel = &m_Pixels[(x + (y * m_Width)) * info.NumComponents * m_Depth];
	for (uint32 i = 0; i < info.NumComponents; ++i)
	{
		pPixel[i] = (unsigned char)(color[i] * 255);
	}
	return true;
}

bool Image::SetPixelInt(uint32 x, uint32 y, unsigned int color)
{
	const FormatInfo& info = RHI::GetFormatInfo(m_Format);
	check(!info.IsBC, "Can't get pixel data from block compressed texture");
	if (x + y * m_Width >= (int)m_Pixels.size())
	{
		return false;
	}
	unsigned char* pPixel = &m_Pixels[(x + (y * m_Width)) * info.NumComponents * m_Depth];
	for (uint32 i = 0; i < info.NumComponents; ++i)
	{
		pPixel[i] = reinterpret_cast<const unsigned char*>(&color)[i];
	}
	return true;
}

Color Image::GetPixel(uint32 x, uint32 y) const
{
	const FormatInfo& info = RHI::GetFormatInfo(m_Format);
	check(!info.IsBC, "Can't get pixel data from block compressed texture");
	Color c = {};
	if (x + y * m_Width >= (int)m_Pixels.size())
	{
		return c;
	}
	const unsigned char* pPixel = &m_Pixels[(x + (y * m_Width)) * info.NumComponents * m_Depth];
	for (uint32 i = 0; i < info.NumComponents; ++i)
	{
		reinterpret_cast<float*>(&c)[i] = (float)pPixel[i] / 255.0f;
	}
	return c;
}

uint32 Image::GetPixelInt(uint32 x, uint32 y) const
{
	const FormatInfo& info = RHI::GetFormatInfo(m_Format);
	check(!info.IsBC, "Can't get pixel data from block compressed texture");
	uint32 c = 0;
	if (x + y * m_Width >= (uint32)m_Pixels.size())
	{
		return c;
	}
	const unsigned char* pPixel = &m_Pixels[(x + (y * m_Width)) * info.NumComponents * m_Depth];
	for (uint32 i = 0; i < info.NumComponents; ++i)
	{
		c <<= 8;
		c |= pPixel[i];
	}
	c <<= 8 * (4 - info.NumComponents);
	return c;
}

const unsigned char* Image::GetData(uint32 mipLevel) const
{
	uint64 offset = 0;
	for (uint32 mip = 0; mip < mipLevel; ++mip)
	{
		offset += RHI::GetTextureMipByteSize(m_Format, m_Width, m_Height, m_Depth, mip);
	}
	return m_Pixels.data() + offset;
}

bool Image::LoadSTB(Stream& stream)
{
	int components = 0;

	uint32 size = stream.GetLength();
	Array<uint8> buffer(size);
	stream.Read(buffer.data(), size);

	m_IsHdr = stbi_is_hdr_from_memory(buffer.data(), size);

	if (m_IsHdr)
	{
		int width, height;
		float* pPixels = stbi_loadf_from_memory(buffer.data(), size, &width, &height, &components, 4);
		if (pPixels == nullptr)
		{
			return false;
		}
		m_Width = (uint32)width;
		m_Height = (uint32)height;
		m_Depth = 1;
		m_Format = ResourceFormat::RGBA32_FLOAT;
		m_Pixels.resize(m_Width * m_Height * 4 * sizeof(float));
		memcpy(m_Pixels.data(), pPixels, m_Pixels.size());
		stbi_image_free(pPixels);
		return true;
	}
	else
	{
		int width, height;
		unsigned char* pPixels = stbi_load_from_memory(buffer.data(), size, &width, &height, &components, 4);
		if (pPixels == nullptr)
		{
			return false;
		}
		m_Width = (uint32)width;
		m_Height = (uint32)height;
		m_Depth = 1;
		m_Format = ResourceFormat::RGBA8_UNORM;
		m_Pixels.resize(m_Width * m_Height * 4);
		memcpy(m_Pixels.data(), pPixels, m_Pixels.size());
		stbi_image_free(pPixels);
		return true;
	}
}

bool Image::LoadDDS(Stream& stream)
{
	// .DDS subheader.
#pragma pack(push,1)
	struct PixelFormatHeader
	{
		uint32 dwSize;
		uint32 dwFlags;
		uint32 dwFourCC;
		uint32 dwRGBBitCount;
		uint32 dwRBitMask;
		uint32 dwGBitMask;
		uint32 dwBBitMask;
		uint32 dwABitMask;
	};
#pragma pack(pop)

	// .DDS header.
#pragma pack(push,1)
	struct FileHeader
	{
		uint32 dwSize;
		uint32 dwFlags;
		uint32 dwHeight;
		uint32 dwWidth;
		uint32 dwLinearSize;
		uint32 dwDepth;
		uint32 dwMipMapCount;
		uint32 dwReserved1[11];
		PixelFormatHeader ddpf;
		uint32 dwCaps;
		uint32 dwCaps2;
		uint32 dwCaps3;
		uint32 dwCaps4;
		uint32 dwReserved2;
	};
#pragma pack(pop)

	// .DDS 10 header.
#pragma pack(push,1)
	struct DX10FileHeader
	{
		uint32 dxgiFormat;
		uint32 resourceDimension;
		uint32 miscFlag;
		uint32 arraySize;
		uint32 reserved;
	};
#pragma pack(pop)

	enum DDS_CAP_ATTRIBUTE
	{
		DDSCAPS_COMPLEX = 0x00000008U,
		DDSCAPS_TEXTURE = 0x00001000U,
		DDSCAPS_MIPMAP = 0x00400000U,
		DDSCAPS2_VOLUME = 0x00200000U,
		DDSCAPS2_CUBEMAP = 0x00000200U,
	};

	auto MakeFourCC = [](uint32 a, uint32 b, uint32 c, uint32 d) { return a | (b << 8u) | (c << 16u) | (d << 24u); };

	constexpr const char pMagic[] = "DDS ";

	char magic[4];
	stream.Read(magic, 4);
	if (memcmp(pMagic, magic, 4) != 0)
	{
		return false;
	}

	FileHeader header;
	stream.Read(&header, sizeof(FileHeader));

	if (header.dwSize == sizeof(FileHeader) &&
		header.ddpf.dwSize == sizeof(PixelFormatHeader))
	{
		m_sRgb = false;
		uint32 bpp = header.ddpf.dwRGBBitCount;

		uint32 fourCC = header.ddpf.dwFourCC;
		bool hasDxgi = fourCC == MakeFourCC('D', 'X', '1', '0');

		DX10FileHeader dx10Header{};
		if (hasDxgi)
		{
			stream.Read(&dx10Header, sizeof(DX10FileHeader));

			auto ConvertDX10Format = [](DXGI_FORMAT format, ResourceFormat& outFormat, bool& outSRGB)
				{
					if (format == DXGI_FORMAT_BC1_UNORM) { outFormat = ResourceFormat::BC1_UNORM;			outSRGB = false;	return; }
					if (format == DXGI_FORMAT_BC1_UNORM_SRGB) { outFormat = ResourceFormat::BC1_UNORM;			outSRGB = true;		return; }
					if (format == DXGI_FORMAT_BC2_UNORM) { outFormat = ResourceFormat::BC2_UNORM;			outSRGB = false;	return; }
					if (format == DXGI_FORMAT_BC2_UNORM_SRGB) { outFormat = ResourceFormat::BC2_UNORM;			outSRGB = true;		return; }
					if (format == DXGI_FORMAT_BC3_UNORM) { outFormat = ResourceFormat::BC3_UNORM;			outSRGB = false;	return; }
					if (format == DXGI_FORMAT_BC4_UNORM) { outFormat = ResourceFormat::BC4_UNORM;			outSRGB = false;	return; }
					if (format == DXGI_FORMAT_BC5_UNORM) { outFormat = ResourceFormat::BC5_UNORM;			outSRGB = false;	return; }
					if (format == DXGI_FORMAT_BC6H_UF16) { outFormat = ResourceFormat::BC6H_UFLOAT;			outSRGB = false;	return; }
					if (format == DXGI_FORMAT_BC7_UNORM) { outFormat = ResourceFormat::BC7_UNORM;			outSRGB = false;	return; }
					if (format == DXGI_FORMAT_BC7_UNORM_SRGB) { outFormat = ResourceFormat::BC7_UNORM;			outSRGB = true;		return; }
					if (format == DXGI_FORMAT_R32G32B32A32_FLOAT) { outFormat = ResourceFormat::RGBA32_FLOAT;			outSRGB = false;	return; }
					if (format == DXGI_FORMAT_R32G32_FLOAT) { outFormat = ResourceFormat::RG32_FLOAT;			outSRGB = false;	return; }
				};
			ConvertDX10Format((DXGI_FORMAT)dx10Header.dxgiFormat, m_Format, m_sRgb);
		}
		else
		{
			switch (fourCC)
			{
			case MakeFourCC('B', 'C', '4', 'U'):	m_Format = ResourceFormat::BC4_UNORM;		break;
			case MakeFourCC('D', 'X', 'T', '1'):	m_Format = ResourceFormat::BC1_UNORM;		break;
			case MakeFourCC('D', 'X', 'T', '3'):	m_Format = ResourceFormat::BC2_UNORM;		break;
			case MakeFourCC('D', 'X', 'T', '5'):	m_Format = ResourceFormat::BC3_UNORM;		break;
			case MakeFourCC('B', 'C', '5', 'U'):	m_Format = ResourceFormat::BC5_UNORM;		break;
			case MakeFourCC('A', 'T', 'I', '2'):	m_Format = ResourceFormat::BC5_UNORM;		break;
			case 0:
				if (bpp == 32)
				{
					auto TestMask = [=](uint32 r, uint32 g, uint32 b, uint32 a)
						{
							return header.ddpf.dwRBitMask == r &&
								header.ddpf.dwGBitMask == g &&
								header.ddpf.dwBBitMask == b &&
								header.ddpf.dwABitMask == a;
						};

					if (TestMask(0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000))
					{
						m_Format = ResourceFormat::RGBA8_UNORM;
					}
					else if (TestMask(0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000))
					{
						m_Format = ResourceFormat::BGRA8_UNORM;
					}
					else
					{
						return false;
					}
				}
				break;
			default:
				return false;
			}
		}

		bool isCubemap = (header.dwCaps2 & 0x0000FC00U) != 0 || (hasDxgi && (dx10Header.miscFlag & 0x4) != 0);
		uint32 imageChainCount = 1;
		if (isCubemap)
		{
			imageChainCount = 6;
			m_IsCubemap = true;
		}
		else if (hasDxgi && dx10Header.arraySize > 1)
		{
			imageChainCount = dx10Header.arraySize;
			m_IsArray = true;
		}

		Image* pCurrentImage = this;
		for (uint32 imageIdx = 0; imageIdx < imageChainCount; ++imageIdx)
		{
			pCurrentImage->SetSize(header.dwWidth, header.dwHeight, header.dwDepth, header.dwMipMapCount);
			stream.Read(m_Pixels.data(), (uint32)m_Pixels.size());

			if (imageIdx < imageChainCount - 1)
			{
				pCurrentImage->m_pNextImage = std::make_unique<Image>(m_Format);
				pCurrentImage = pCurrentImage->m_pNextImage.get();
			}
		}
	}
	else
	{
		return false;
	}
	return true;
}

void Image::Save(const char* pFilePath)
{
	const FormatInfo& info = RHI::GetFormatInfo(m_Format);
	String extension = Paths::GetFileExtenstion(pFilePath);
	if (extension == "png")
	{
		int result = stbi_write_png(pFilePath, m_Width, m_Height, info.NumComponents, m_Pixels.data(), m_Width * 4);
		check(result);
	}
	else if (extension == "jpg")
	{
		int result = stbi_write_jpg(pFilePath, m_Width, m_Height, info.NumComponents, m_Pixels.data(), 70);
		check(result);
	}
}
