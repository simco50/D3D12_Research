#include "stdafx.h"
#include "Image.h"

//#define STBI_NO_PSD
#define STBI_NO_GIF
#define STBI_NO_PIC
#define STBI_NO_PNM

#define STB_IMAGE_IMPLEMENTATION
#include "Stb/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "Stb/stb_image_write.h"
#include <fstream>
#include "Core/Paths.h"

bool Image::Load(const char* inputStream)
{
	const std::string extension = Paths::GetFileExtenstion(inputStream);
	bool success = false;
	if (extension == "dds")
	{
		success = LoadDds(inputStream);
	}
	//If not one of the above, load with Stbi by default (jpg, png, tga, bmp, ...)
	else
	{
		success = LoadStbi(inputStream);
	}
	return success;
}

bool Image::Load(const void* pInPixels, size_t dataSize, const char* pFormatHint)
{
	m_Components = 4;
	m_Depth = 1;
	int components = 0;

	unsigned char* pPixels = stbi_load_from_memory((stbi_uc*)pInPixels, (int)dataSize, &m_Width, &m_Height, &components, m_Components);
	if (pPixels == nullptr)
	{
		return false;
	}
	m_BBP = sizeof(char) * 8 * m_Components;
	m_Format = ImageFormat::RGBA;
	m_Pixels.resize(m_Width * m_Height * m_Components);
	memcpy(m_Pixels.data(), pPixels, m_Pixels.size());
	stbi_image_free(pPixels);
	return true;
}

bool Image::SetSize(int x, int y, int components)
{
	m_Width = x;
	m_Height = y;
	m_Depth = 1;
	m_Components = components;
	m_Pixels.clear();
	m_Pixels.resize(x * y * components);

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

bool Image::SetPixel(int x, int y, const Color& color)
{
	checkf(!D3D::IsBlockCompressFormat((DXGI_FORMAT)TextureFormatFromCompressionFormat(m_Format, m_sRgb)), "Can't set pixel data from block compressed texture");
	if (x + y * m_Width >= (int)m_Pixels.size())
	{
		return false;
	}
	unsigned char* pPixel = &m_Pixels[(x + (y * m_Width)) * m_Components * m_Depth];
	for (int i = 0; i < m_Components; ++i)
	{
		pPixel[i] = (unsigned char)(color[i] * 255);
	}
	return true;
}

bool Image::SetPixelInt(int x, int y, unsigned int color)
{
	checkf(!D3D::IsBlockCompressFormat((DXGI_FORMAT)TextureFormatFromCompressionFormat(m_Format, m_sRgb)), "Can't set pixel data from block compressed texture");
	if (x + y * m_Width >= (int)m_Pixels.size())
	{
		return false;
	}
	unsigned char* pPixel = &m_Pixels[(x + (y * m_Width)) * m_Components * m_Depth];
	for (int i = 0; i < m_Components; ++i)
	{
		pPixel[i] = reinterpret_cast<const unsigned char*>(&color)[i];
	}
	return true;
}

Color Image::GetPixel(int x, int y) const
{
	checkf(!D3D::IsBlockCompressFormat((DXGI_FORMAT)TextureFormatFromCompressionFormat(m_Format, m_sRgb)), "Can't get pixel data from block compressed texture");
	Color c = {};
	if (x + y * m_Width >= (int)m_Pixels.size())
	{
		return c;
	}
	const unsigned char* pPixel = &m_Pixels[(x + (y * m_Width)) * m_Components * m_Depth];
	for (int i = 0; i < m_Components; ++i)
	{
		reinterpret_cast<float*>(&c)[i] = (float)pPixel[i] / 255.0f;
	}
	return c;
}

unsigned int Image::GetPixelInt(int x, int y) const
{
	checkf(!D3D::IsBlockCompressFormat((DXGI_FORMAT)TextureFormatFromCompressionFormat(m_Format, m_sRgb)), "Can't get pixel data from block compressed texture");
	unsigned int c = 0;
	if (x + y * m_Width >= (int)m_Pixels.size())
	{
		return c;
	}
	const unsigned char* pPixel = &m_Pixels[(x + (y * m_Width)) * m_Components * m_Depth];
	for (int i = 0; i < m_Components; ++i)
	{
		c <<= 8;
		c |= pPixel[i];
	}
	c <<= 8 * (4 - m_Components);
	return c;
}

const unsigned char* Image::GetData(int mipLevel) const
{
	if (mipLevel >= m_MipLevels)
	{
		return nullptr;
	}
	uint32 offset = mipLevel == 0 ? 0 : m_MipLevelDataOffsets[mipLevel];
	return m_Pixels.data() + offset;
}

MipLevelInfo Image::GetMipInfo(int mipLevel) const
{
	if (mipLevel >= m_MipLevels)
	{
		return MipLevelInfo();
	}
	MipLevelInfo info;
	GetSurfaceInfo(m_Width, m_Height, m_Depth, mipLevel, info);
	return info;
}

bool Image::GetSurfaceInfo(int width, int height, int depth, int mipLevel, MipLevelInfo& mipLevelInfo) const
{
	if (mipLevel >= m_MipLevels)
	{
		return false;
	}

	mipLevelInfo.Width = std::max(1, width >> mipLevel);
	mipLevelInfo.Height = std::max(1, height >> mipLevel);
	mipLevelInfo.Depth = std::max(1, depth >> mipLevel);

	if (m_Format == ImageFormat::RGBA || m_Format == ImageFormat::BGRA)
	{
		mipLevelInfo.RowSize = mipLevelInfo.Width * m_BBP / 8;
		mipLevelInfo.Rows = mipLevelInfo.Height;
		mipLevelInfo.DataSize = mipLevelInfo.Depth * mipLevelInfo.Rows * mipLevelInfo.RowSize;
	}
	else if (IsCompressed())
	{
		int blockSize = (m_Format == ImageFormat::BC1 || m_Format == ImageFormat::BC4) ? 8 : 16;
		mipLevelInfo.RowSize = ((mipLevelInfo.Width + 3) / 4) * blockSize;
		mipLevelInfo.Rows = (mipLevelInfo.Height + 3) / 4;
		mipLevelInfo.DataSize = mipLevelInfo.Depth * mipLevelInfo.Rows * mipLevelInfo.RowSize;
	}
	else
	{
		return false;
	}
	return true;
}

unsigned int Image::TextureFormatFromCompressionFormat(const ImageFormat& format, bool sRgb)
{
	switch (format)
	{
	case ImageFormat::RGBA:		return sRgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
	case ImageFormat::BGRA:		return sRgb ? DXGI_FORMAT_B8G8R8A8_UNORM_SRGB : DXGI_FORMAT_B8G8R8A8_UNORM;
	case ImageFormat::RGB32:	return DXGI_FORMAT_R32G32B32_FLOAT;
	case ImageFormat::RGBA16:	return DXGI_FORMAT_R16G16B16A16_FLOAT;
	case ImageFormat::RGBA32:	return DXGI_FORMAT_R32G32B32A32_FLOAT;
	case ImageFormat::BC1:		return sRgb ? DXGI_FORMAT_BC1_UNORM_SRGB : DXGI_FORMAT_BC1_UNORM;
	case ImageFormat::BC2:		return sRgb ? DXGI_FORMAT_BC2_UNORM_SRGB : DXGI_FORMAT_BC2_UNORM;
	case ImageFormat::BC3:		return sRgb ? DXGI_FORMAT_BC3_UNORM_SRGB : DXGI_FORMAT_BC3_UNORM;
	case ImageFormat::BC4:		return DXGI_FORMAT_BC4_UNORM;
	case ImageFormat::BC5:		return DXGI_FORMAT_BC5_UNORM;
	case ImageFormat::BC6H:		return DXGI_FORMAT_BC6H_UF16;
	case ImageFormat::BC7:		return sRgb ? DXGI_FORMAT_BC7_UNORM_SRGB : DXGI_FORMAT_BC7_UNORM;
	default:
		noEntry();
		return DXGI_FORMAT_UNKNOWN;
	}
}

bool Image::LoadStbi(const char* inputStream)
{
	m_Components = 4;
	m_Depth = 1;
	int components = 0;

	m_IsHdr = stbi_is_hdr(inputStream);
	if (m_IsHdr)
	{
		float* pPixels = stbi_loadf(inputStream, &m_Width, &m_Height, &components, m_Components);
		if (pPixels == nullptr)
		{
			return false;
		}
		m_BBP = sizeof(float) * 8 * m_Components;
		m_Format = ImageFormat::RGBA32;
		m_Pixels.resize(m_Width * m_Height * m_Components * sizeof(float));
		memcpy(m_Pixels.data(), pPixels, m_Pixels.size());
		stbi_image_free(pPixels);
		return true;
	}
	else
	{
		unsigned char* pPixels = stbi_load(inputStream, &m_Width, &m_Height, &components, m_Components);
		if (pPixels == nullptr)
		{
			return false;
		}
		m_BBP = sizeof(char) * 8 * m_Components;
		m_Format = ImageFormat::RGBA;
		m_Pixels.resize(m_Width * m_Height * m_Components);
		memcpy(m_Pixels.data(), pPixels, m_Pixels.size());
		stbi_image_free(pPixels);
		return true;
	}
}

bool Image::LoadDds(const char* inputStream)
{
	std::ifstream fStream(inputStream, std::ios::binary);

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

	enum IMAGE_FORMAT
	{
		R8G8B8A8_UNORM = 28,
		R8G8B8A8_UNORM_SRGB = 26,
		BC1_UNORM = 71,
		BC1_UNORM_SRGB = 72,
		BC2_UNORM = 74,
		BC2_UNORM_SRGB = 75,
		BC3_UNORM = 77,
		BC3_UNORM_SRGB = 78,
		BC4_UNORM = 80,
		BC5_UNORM = 83,
		DXGI_FORMAT_BC6H_UF16 = 95,
		DXGI_FORMAT_BC7_UNORM = 98,
		DXGI_FORMAT_BC7_UNORM_SRGB = 99,
	};

	enum DDS_CAP_ATTRIBUTE
	{
		DDSCAPS_COMPLEX = 0x00000008U,
		DDSCAPS_TEXTURE = 0x00001000U,
		DDSCAPS_MIPMAP = 0x00400000U,
		DDSCAPS2_VOLUME = 0x00200000U,
		DDSCAPS2_CUBEMAP = 0x00000200U,
	};

#ifndef MAKEFOURCC
#define MAKEFOURCC(a, b, c, d) (unsigned int)((unsigned char)(a) | (unsigned char)(b) << 8 | (unsigned char)(c) << 16 | (unsigned char)(d) << 24)
#endif
	char magic[5];
	magic[4] = '\0';
	fStream.read(&magic[0], 4);

	if (strcmp(magic, "DDS ") != 0)
	{
		return false;
	}

	FileHeader header;
	fStream.read((char*)&header, sizeof(FileHeader));

	if (header.dwSize == sizeof(FileHeader) &&
		header.ddpf.dwSize == sizeof(PixelFormatHeader))
	{
		m_BBP = header.ddpf.dwRGBBitCount;

		uint32 fourCC = header.ddpf.dwFourCC;
		char fourCCStr[5];
		fourCCStr[4] = '\0';
		memcpy(fourCCStr, &fourCC, 4);
		bool hasDxgi = fourCC == MAKEFOURCC('D', 'X', '1', '0');
		DX10FileHeader* pDx10Header = nullptr;

		if (hasDxgi)
		{
			DX10FileHeader dds10Header = {};
			pDx10Header = &dds10Header;
			fStream.read((char*)&dds10Header, sizeof(DX10FileHeader));

			switch (dds10Header.dxgiFormat)
			{
			case IMAGE_FORMAT::BC1_UNORM_SRGB:
				m_Components = 3;
				m_sRgb = true;
			case IMAGE_FORMAT::BC1_UNORM:
				m_Format = ImageFormat::BC1;
				break;
			case IMAGE_FORMAT::BC2_UNORM_SRGB:
				m_Components = 4;
				m_sRgb = true;
			case IMAGE_FORMAT::BC2_UNORM:
				m_Format = ImageFormat::BC2;
				break;
			case IMAGE_FORMAT::BC3_UNORM_SRGB:
				m_Components = 4;
				m_sRgb = true;
			case IMAGE_FORMAT::BC3_UNORM:
				m_Format = ImageFormat::BC3;
				break;
			case IMAGE_FORMAT::BC4_UNORM:
				m_Components = 4;
				m_Format = ImageFormat::BC4;
				break;
			case IMAGE_FORMAT::BC5_UNORM:
				m_Components = 4;
				m_Format = ImageFormat::BC5;
				break;
			case IMAGE_FORMAT::DXGI_FORMAT_BC6H_UF16:
				m_Components = 3;
				m_Format = ImageFormat::BC6H;
				break;
			case IMAGE_FORMAT::DXGI_FORMAT_BC7_UNORM_SRGB:
				m_Components = 4;
				m_sRgb = true;
			case IMAGE_FORMAT::DXGI_FORMAT_BC7_UNORM:
				m_Format = ImageFormat::BC7;
				break;
			case IMAGE_FORMAT::R8G8B8A8_UNORM_SRGB:
				m_Components = 4;
				m_sRgb = true;
			case IMAGE_FORMAT::R8G8B8A8_UNORM:
				m_Format = ImageFormat::RGBA;
				break;
			default:
				return false;
			}
		}
		else
		{
			switch (fourCC)
			{
			case MAKEFOURCC('D', 'X', 'T', '1'):
				m_Format = ImageFormat::BC1;
				m_Components = 3;
				m_sRgb = false;
				break;
			case MAKEFOURCC('D', 'X', 'T', '3'):
				m_Format = ImageFormat::BC2;
				m_Components = 4;
				m_sRgb = false;
				break;
			case MAKEFOURCC('D', 'X', 'T', '5'):
				m_Format = ImageFormat::BC3;
				m_Components = 4;
				m_sRgb = false;
				break;
			case MAKEFOURCC('B', 'C', '5', 'U'):
			case MAKEFOURCC('A', 'T', 'I', '2'):
				m_Format = ImageFormat::BC5;
				m_Components = 2;
				m_sRgb = false;
				break;
			case 0:
				if (m_BBP == 32)
				{
					m_Components = 4;
#define ISBITMASK(r, g, b, a) (header.ddpf.dwRBitMask == (r) && header.ddpf.dwGBitMask == (g) && header.ddpf.dwBBitMask == (b) && header.ddpf.dwABitMask == (a))
					if (ISBITMASK(0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000))
					{
						m_Format = ImageFormat::RGBA;
					}
					else if (ISBITMASK(0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000))
					{
						m_Format = ImageFormat::BGRA;
					}
					else
					{
						return false;
					}
				}
#undef ISBITMASK
#undef MAKEFOURCC
				break;
			default:
				return false;
			}
		}

		bool isCubemap = (header.dwCaps2 & 0x0000FC00U) != 0 || (hasDxgi && (pDx10Header->miscFlag & 0x4) != 0);
		uint32 imageChainCount = 1;
		if (isCubemap)
		{
			imageChainCount = 6;
			m_IsCubemap = true;
		}
		else if (hasDxgi && pDx10Header->arraySize > 1)
		{
			imageChainCount = pDx10Header->arraySize;
			m_IsArray = true;
		}
		uint32 totalDataSize = 0;
		m_MipLevels = std::max(1, (int)header.dwMipMapCount);
		for (int mipLevel = 0; mipLevel < m_MipLevels; ++mipLevel)
		{
			MipLevelInfo mipInfo;
			GetSurfaceInfo(header.dwWidth, header.dwHeight, header.dwDepth, mipLevel, mipInfo);
			m_MipLevelDataOffsets[mipLevel] = totalDataSize;
			totalDataSize += mipInfo.DataSize;
		}

		Image* pCurrentImage = this;
		for (uint32 imageIdx = 0; imageIdx < imageChainCount; ++imageIdx)
		{
			pCurrentImage->m_Pixels.resize(totalDataSize);
			pCurrentImage->m_Width = header.dwWidth;
			pCurrentImage->m_Height = header.dwHeight;
			pCurrentImage->m_Depth = header.dwDepth;
			pCurrentImage->m_Format = m_Format;
			pCurrentImage->m_BBP = m_BBP;
			fStream.read((char*)pCurrentImage->m_Pixels.data(), pCurrentImage->m_Pixels.size());

			if (imageIdx < imageChainCount - 1)
			{
				pCurrentImage->m_pNextImage = std::make_unique<Image>();
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
	int result = stbi_write_jpg(pFilePath, m_Width, m_Height, m_Components, m_Pixels.data(), 100);
	check(result);
}
