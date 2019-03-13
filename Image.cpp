#include "FluxEngine.h"
#include "Image.h"

#include "FileSystem/File/PhysicalFile.h"

//#define STBI_NO_PSD
#define STBI_NO_GIF
#define STBI_NO_PIC
#define STBI_NO_PNM

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ASSERT(x) check(x)
#include "External/Stb/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "External/Stb/stb_image_write.h"

#define TINYEXR_IMPLEMENTATION
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "External/TinyExr/tinyexr.h"

#include <SDL_surface.h>

namespace STBI
{
	int ReadCallback(void* pUser, char* pData, int size)
	{
		InputStream* pStream = static_cast<InputStream*>(pUser);
		if (pStream == nullptr)
		{
			return 0;
		}
		return (int)pStream->Read(pData, (size_t)size);
	}

	void SkipCallback(void* pUser, int n)
	{
		InputStream* pStream = static_cast<InputStream*>(pUser);
		if (pStream)
		{
			pStream->MovePointer(n);
		}
	}

	int EofCallback(void* pUser)
	{
		InputStream* pStream = static_cast<InputStream*>(pUser);
		if (pStream == nullptr)
		{
			return 1;
		}
		return pStream->GetPointer() >= pStream->GetSize() ? 1 : 0;
	}
}

Image::Image(Context* pContext)
	: Resource(pContext)
{

}

Image::~Image()
{

}

bool Image::Load(InputStream& inputStream)
{
	AUTOPROFILE_DESC(Image_Load, inputStream.GetSource().c_str());

	const std::string extenstion = Paths::GetFileExtenstion(inputStream.GetSource());
	bool success;
	if (extenstion == "dds")
	{
		success = LoadDds(inputStream);
	}
	else if (extenstion == "exr")
	{
		success = LoadExr(inputStream);
	}
	//If not one of the above, load with Stbi by default (jpg, png, tga, bmp, ...)
	else
	{
		success = LoadStbi(inputStream);
	}
	return success;
}

bool Image::Save(OutputStream& outputStream)
{
	return SavePng(outputStream);
}

bool Image::Save(const std::string& filePath)
{
	std::string extension = Paths::GetFileExtenstion(filePath);
	PhysicalFile file(filePath);
	if (file.OpenWrite() == false)
	{
		return false;
	}
	if (extension == "png")
	{
		return SavePng(file);
	}
	if (extension == "jpg")
	{
		return SaveJpg(file);
	}
	if (extension == "tga")
	{
		return SaveTga(file);
	}
	if (extension == "bmp")
	{
		return SaveBmp(file);
	}
	FLUX_LOG(Warning, "[Image::Save] > File extension '%s' is not supported", extension.c_str());
	return false;
}

bool Image::LoadLUT(InputStream& inputStream)
{
	AUTOPROFILE(Image_Load);
	m_Components = 4;
	stbi_io_callbacks callbacks;
	callbacks.read = STBI::ReadCallback;
	callbacks.skip = STBI::SkipCallback;
	callbacks.eof = STBI::EofCallback;
	int components = 0;
	unsigned char*  pPixels = stbi_load_from_callbacks(&callbacks, &inputStream, &m_Width, &m_Height, &components, m_Components);
	if (pPixels == nullptr)
	{
		return false;
	}

	m_BBP = 32;
	m_Pixels.resize(m_Height * m_Width * m_Components);
	m_Width = m_Depth = m_Height = 16;

	int* c3D = (int*)m_Pixels.data();
	int* c2D = (int*)pPixels;
	const int dim = m_Height;
	for (int z = 0; z < dim; ++z)
	{
		for (int y = 0; y < dim; ++y)
		{
			for (int x = 0; x < dim; ++x)
			{
				c3D[x + y * dim + z * dim * dim] = c2D[x + y * dim * dim + z * dim];
			}
		}
	}

	stbi_image_free(pPixels);

	SetMemoryUsage((unsigned int)m_Pixels.size());

	return true;
}

bool Image::SavePng(OutputStream& outputStream)
{
	const int result = stbi_write_png_to_func([](void *context, void *data, int size)
	{
		OutputStream* pStream = static_cast<OutputStream*>(context);
		if (!pStream->Write((char*)data, size))
		{
			return;
		}
	}, &outputStream, m_Width, m_Height, m_Components, m_Pixels.data(), m_Width * m_Components * m_Depth);
	return result > 0;
}


bool Image::SaveBmp(OutputStream& outputStream)
{
	const int result = stbi_write_bmp_to_func([](void *context, void *data, int size)
	{
		OutputStream* pStream = static_cast<OutputStream*>(context);
		if (!pStream->Write((char*)data, size))
		{
			return;
		}
	}, &outputStream, m_Width, m_Height, m_Components, m_Pixels.data());
	return result > 0;
}

bool Image::SaveJpg(OutputStream& outputStream, const int quality /*= 100*/)
{
	const int result = stbi_write_jpg_to_func([](void *context, void *data, int size)
	{
		OutputStream* pStream = static_cast<OutputStream*>(context);
		if (!pStream->Write((char*)data, size))
		{
			return;
		}
	}, &outputStream, m_Width, m_Height, m_Components, m_Pixels.data(), quality);
	return result > 0;
}

bool Image::SaveTga(OutputStream& outputStream)
{
	const int result = stbi_write_tga_to_func([](void *context, void *data, int size)
	{
		OutputStream* pStream = static_cast<OutputStream*>(context);
		if (!pStream->Write((char*)data, size))
		{
			return;
		}
	}, &outputStream, m_Width, m_Height, m_Components, m_Pixels.data());
	return result > 0;
}

bool Image::SetSize(const int x, const int y, const int components)
{
	m_Width = x;
	m_Height = y;
	m_Depth = 1;
	m_Components = components;
	m_Pixels.clear();
	m_Pixels.resize(x * y * components);

	SetMemoryUsage((unsigned int)m_Pixels.size());

	return true;
}

bool Image::SetData(const unsigned int* pPixels)
{
	memcpy(m_Pixels.data(), pPixels, m_Pixels.size() * m_Depth * m_Components);
	return true;
}

bool Image::SetPixel(const int x, const int y, const Color& color)
{
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

bool Image::SetPixelInt(const int x, const int y, const unsigned int color)
{
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

Color Image::GetPixel(const int x, const int y) const
{
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

unsigned int Image::GetPixelInt(const int x, const int y) const
{
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

SDL_Surface* Image::GetSDLSurface()
{
	if (m_Pixels.size() == 0)
	{
		return nullptr;
	}

	// Assume little-endian for all the supported platforms
	unsigned rMask = 0x000000ff;
	unsigned gMask = 0x0000ff00;
	unsigned bMask = 0x00ff0000;
	unsigned aMask = 0xff000000;

	SDL_Surface* surface = SDL_CreateRGBSurface(0, m_Width, m_Height, 4 * 8, rMask, gMask, bMask, aMask);
	SDL_LockSurface(surface);

	unsigned char* destination = reinterpret_cast<unsigned char*>(surface->pixels);
	unsigned char* source = m_Pixels.data() ;
	for (int i = 0; i < m_Height; ++i)
	{
		memcpy(destination, source, 4 * m_Width);
		destination += surface->pitch;
		source += 4 * m_Width;
	}

	SDL_UnlockSurface(surface);

	return surface;
}

const unsigned char* Image::GetData(int mipLevel) const
{
	if (mipLevel >= m_MipLevels)
	{
		FLUX_LOG(Warning, "[Image::GetSurfaceInfo] Requested mip level %d but only has %d mips", mipLevel, m_MipLevels);
		return nullptr;
	}
	uint32 offset = mipLevel == 0 ? 0 : m_MipLevelDataOffsets[mipLevel];
	return m_Pixels.data() + offset;
}

MipLevelInfo Image::GetMipInfo(int mipLevel) const
{
	if (mipLevel >= m_MipLevels)
	{
		FLUX_LOG(Warning, "[Image::GetSurfaceInfo] Requested mip level %d but only has %d mips", mipLevel, m_MipLevels);
		return MipLevelInfo();
	}
	MipLevelInfo info;
	GetSurfaceInfo(m_Width, m_Height, m_Depth, mipLevel, info);
	return info;
}

bool Image::LoadStbi(InputStream& inputStream)
{
	m_Components = 4;
	m_Depth = 1;
	stbi_io_callbacks callbacks;
	callbacks.read = STBI::ReadCallback;
	callbacks.skip = STBI::SkipCallback;
	callbacks.eof = STBI::EofCallback;

	int components = 0;

	m_IsHdr = stbi_is_hdr_from_callbacks(&callbacks, &inputStream);
	inputStream.SetPointer(0);
	if (m_IsHdr)
	{
		float* pPixels = stbi_loadf_from_callbacks(&callbacks, &inputStream, &m_Width, &m_Height, &components, m_Components);
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
		unsigned char* pPixels = stbi_load_from_callbacks(&callbacks, &inputStream, &m_Width, &m_Height, &components, m_Components);
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

bool Image::LoadExr(InputStream& inputStream)
{
	std::vector<unsigned char> buffer;
	inputStream.ReadAllBytes(buffer);

	const char* errorMessage = nullptr;
	EXRVersion exrVersion;

	int result = ParseEXRVersionFromMemory(&exrVersion, buffer.data(), buffer.size());
	if (result != 0)
	{
		FLUX_LOG(Warning, "[HDRImage::LoadExr] Failed to read Exr version");
		return false;
	}

	if (exrVersion.multipart)
	{
		FLUX_LOG(Warning, "[HDRImage::LoadExr] Exr file is multipart. This is not supported");
		return false;
	}

	EXRHeader exrHeader;
	InitEXRHeader(&exrHeader);

	result = ParseEXRHeaderFromMemory(&exrHeader, &exrVersion, buffer.data(), buffer.size(), &errorMessage);
	if (result != 0)
	{
		FLUX_LOG(Warning, "[HDRImage::LoadExr] Failed to parse Exr header: %s", errorMessage);
		FreeEXRErrorMessage(errorMessage);
		return false;
	}

	int pixelType = exrHeader.pixel_types[0];
	for (int i = 1; i < exrHeader.num_channels; i++)
	{
		UNREFERENCED_PARAMETER(pixelType); //For release builds
		checkf(pixelType == exrHeader.pixel_types[i], "[Image::LoadExr] The pixel types of the channels are not equal. This is a requirement");
	}

	//The amount of bytes in a pixel per channel (== BytesPerPixel / Components)
	m_Format = exrHeader.requested_pixel_types[0] == TINYEXR_PIXELTYPE_HALF ? ImageFormat::RGBA16 : ImageFormat::RGBA32;
	int sizePerChannelPerPixel = m_Format == ImageFormat::RGBA16 ? sizeof(int16) : sizeof(int32);

	EXRImage exrImage;
	InitEXRImage(&exrImage);

	result = LoadEXRImageFromMemory(&exrImage, &exrHeader, buffer.data(), buffer.size(), &errorMessage);
	if (result != 0)
	{
		FLUX_LOG(Warning, "[HDRImage::LoadExr] Failed to load Exr from memory: %s", errorMessage);
		FreeEXRErrorMessage(errorMessage);
		return false;
	}

	m_Width = exrImage.width;
	m_Height = exrImage.height;
	m_Components = 4;
	m_IsHdr = true;
	m_BBP = sizePerChannelPerPixel * m_Components * 8;

	m_Pixels.resize(m_Width * m_Height * m_BBP / 8);

	unsigned char* pPixels = m_Pixels.data();

	auto getPixel = [](int x, int y, int width, int pixelSize)
	{
		return (x + y * width) * pixelSize;
	};

	if (exrImage.num_tiles > 0)
	{
		int tileMaxWidth = exrImage.tiles[0].width;
		int tileMaxHeight = exrImage.tiles[0].height;
		for (int i = 0; i < exrImage.num_tiles; ++i)
		{
			const EXRTile& tile = exrImage.tiles[i];
			int startX = tile.offset_x * tileMaxWidth;
			int startY = tile.offset_y * tileMaxHeight;
			for (int y = 0; y < tile.height; ++y)
			{
				for (int x = 0; x < tile.width; ++x)
				{
					int targetIdx = getPixel(startX + x, startY + y, m_Width, sizePerChannelPerPixel * m_Components);
					int sourceIndex = getPixel(x, y, tileMaxWidth, sizePerChannelPerPixel);

					for (int c = 0; c < exrImage.num_channels; ++c)
					{
						memcpy(&pPixels[targetIdx + sizePerChannelPerPixel * c], &tile.images[exrImage.num_channels - 1 - c][sourceIndex], sizePerChannelPerPixel);
					}
				}
			}
		}
	}
	else
	{
		for (int y = 0; y < m_Height; ++y)
		{
			for (int x = 0; x < m_Width; ++x)
			{
				for (int c = 0; c < exrImage.num_channels; ++c)
				{
					int targetIdx = getPixel(x, y, m_Width, m_Components * sizePerChannelPerPixel);
					int sourceIdx = getPixel(x, y, exrImage.width, sizePerChannelPerPixel);
					memcpy(&pPixels[targetIdx + c * sizePerChannelPerPixel], &exrImage.images[exrImage.num_channels - 1 - c][sourceIdx], sizePerChannelPerPixel);
				}
			}
		}
	}

	FreeEXRImage(&exrImage);
	FreeEXRHeader(&exrHeader);

	return true;
}

bool Image::GetSurfaceInfo(int width, int height, int depth, int mipLevel, MipLevelInfo& mipLevelInfo) const
{
	if (mipLevel >= m_MipLevels)
	{
		FLUX_LOG(Warning, "[Image::GetSurfaceInfo] Requested mip level %d but only has %d mips", mipLevel, m_MipLevels);
		return false;
	}

	mipLevelInfo.Width = Math::Max(1, width >> mipLevel);
	mipLevelInfo.Height = Math::Max(1, height >> mipLevel);
	mipLevelInfo.Depth = Math::Max(1, depth >> mipLevel);

	if (m_Format == ImageFormat::RGBA || m_Format == ImageFormat::BGRA)
	{
		mipLevelInfo.RowSize = mipLevelInfo.Width * m_BBP / 8;
		mipLevelInfo.Rows = mipLevelInfo.Height;
		mipLevelInfo.DataSize = mipLevelInfo.Depth * mipLevelInfo.Rows * mipLevelInfo.RowSize;
	}
	else if (IsCompressed())
	{
		int blockSize = (m_Format == ImageFormat::DXT1 || m_Format == ImageFormat::BC4) ? 8 : 16;
		mipLevelInfo.RowSize = ((mipLevelInfo.Width + 3) / 4) * blockSize;
		mipLevelInfo.Rows = (mipLevelInfo.Height + 3) / 4;
		mipLevelInfo.DataSize = mipLevelInfo.Depth * mipLevelInfo.Rows * mipLevelInfo.RowSize;
	}
	else
	{
		FLUX_LOG(Warning, "[CompressedLevel::CalculateSize] Unsupported comression format");
		return false;
	}
	return true;
}

bool Image::LoadDds(InputStream& inputStream)
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
	inputStream.Read(magic, 4);

	if (strcmp(magic, "DDS ") != 0)
	{
		FLUX_LOG(Warning, "[Image::LoadDds] Invalid DDS file magic: %s", magic);
		return false;
	}

	FileHeader header;
	inputStream.Read(&header, sizeof(FileHeader));

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
			inputStream.Read(&dds10Header, sizeof(DX10FileHeader));

			switch (dds10Header.dxgiFormat)
			{
			case IMAGE_FORMAT::BC1_UNORM_SRGB:
				m_Components = 3;
				m_sRgb = true;
			case IMAGE_FORMAT::BC1_UNORM:
				m_Format = ImageFormat::DXT1;
				break;
			case IMAGE_FORMAT::BC2_UNORM_SRGB:
				m_Components = 4;
				m_sRgb = true;
			case IMAGE_FORMAT::BC2_UNORM:
				m_Format = ImageFormat::DXT3;
				break;
			case IMAGE_FORMAT::BC3_UNORM_SRGB:
				m_Components = 4;
				m_sRgb = true;
			case IMAGE_FORMAT::BC3_UNORM:
				m_Format = ImageFormat::DXT5;
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
				FLUX_LOG(Warning, "[Image::LoadDds] Unsupported DXGI Format '%d'. FourCC: %s", dds10Header.dxgiFormat, fourCCStr);
				return false;
			}
		}
		else
		{
			switch (fourCC)
			{
			case MAKEFOURCC('D', 'X', 'T', '1'):
				m_Format = ImageFormat::DXT1;
				m_Components = 3;
				m_sRgb = false;
				break;
			case MAKEFOURCC('D', 'X', 'T', '3'):
				m_Format = ImageFormat::DXT3;
				m_Components = 4;
				m_sRgb = false;
				break;
			case MAKEFOURCC('D', 'X', 'T', '5'):
				m_Format = ImageFormat::DXT5;
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
						FLUX_LOG(Warning, "[Image::LoadDds] Unsupported DDS Format %s", fourCCStr);
						return false;
					}
				}
				break;
			default:
				FLUX_LOG(Warning, "[Image::LoadDds] Unsupported DDS Format %s", fourCCStr);
				return false;
			}
		}

		bool isCubemap = (header.dwCaps2 & 0x0000FC00U) != 0 || (hasDxgi && (pDx10Header->miscFlag & 0x4) != 0);
		uint32 imageChainCount = 1;
		if (isCubemap)
		{
			imageChainCount = 6;
		}
		else if (hasDxgi && pDx10Header->arraySize > 1)
		{
			imageChainCount = pDx10Header->arraySize;
			m_IsArray = true;
		}
		uint32 totalDataSize = 0;
		m_MipLevelDataOffsets.clear();
		m_MipLevels = Math::Max(1, (int)header.dwMipMapCount);
		for (int mipLevel = 0; mipLevel < m_MipLevels; ++mipLevel)
		{
			MipLevelInfo mipInfo;
			GetSurfaceInfo(header.dwWidth, header.dwHeight, header.dwDepth, mipLevel, mipInfo);
			m_MipLevelDataOffsets.push_back(totalDataSize);
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
			inputStream.Read(pCurrentImage->m_Pixels.data(), pCurrentImage->m_Pixels.size());
			pCurrentImage->SetMemoryUsage(totalDataSize);

			if (imageIdx < imageChainCount - 1)
			{
				pCurrentImage->m_pNextImage = std::make_unique<Image>(m_pContext);
				pCurrentImage = pCurrentImage->m_pNextImage.get();
			}
		}
	}
	else
	{
		FLUX_LOG(Warning, "[Image::LoadDds] Invalid data structure sizes");
		return false;
	}
	return true;
}