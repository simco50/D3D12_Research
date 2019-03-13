#pragma once

#include "Resource.h"

struct SDL_Surface;

enum class ImageFormat
{
	RGBA = 0,
	BGRA,
	RGB32,
	RGBA16,
	RGBA32,
	DXT1,
	DXT3,
	DXT5,
	BC4,
	BC5,
	BC6H,
	BC7,
	MAX
};

struct MipLevelInfo
{
	int Width = 0;
	int Height = 0;
	int Depth = 0;
	uint32 Rows = 0;
	uint32 RowSize = 0;
	uint32 DataSize = 0;
};

class Image : public Resource
{
	FLUX_OBJECT(Image, Resource)
	DELETE_COPY(Image)

public:
	explicit Image(Context* pContext);
	virtual ~Image();

	virtual bool Load(InputStream& inputStream) override;
	virtual bool Save(OutputStream& outputStream) override;

	bool LoadLUT(InputStream& inputStream);

	bool Save(const std::string& filePath);
	bool SavePng(OutputStream& outputStream);
	bool SaveBmp(OutputStream& outputStream);
	bool SaveJpg(OutputStream& outputStream, const int quality = 100);
	bool SaveTga(OutputStream& outputStream);

	bool SetSize(const int x, const int y, const int components);
	bool SetData(const unsigned int* pPixels);
	bool SetPixel(const int x, const int y, const Color& color);
	bool SetPixelInt(const int x, const int y, const unsigned int color);

	Color GetPixel(const int x, const int y) const;
	unsigned int GetPixelInt(const int x, const int y) const;

	int GetWidth() const { return m_Width; }
	int GetHeight() const { return m_Height; }
	int GetDepth() const { return m_Depth; }
	int GetComponents() const { return m_Components; }
	bool IsSRGB() const { return m_sRgb; }
	bool IsHDR() const { return m_IsHdr; }

	SDL_Surface* GetSDLSurface();
	unsigned char* GetWritableData() { return m_Pixels.data(); }
	const unsigned char* GetData(int mipLevel = 0) const;

	MipLevelInfo GetMipInfo(int mipLevel) const;
	int GetMipLevels() const { return m_MipLevels; }
	bool IsCompressed() const { return m_Format != ImageFormat::RGBA; }
	ImageFormat GetFormat() const { return m_Format; }
	bool GetSurfaceInfo(int width, int height, int depth, int mipLevel, MipLevelInfo& mipLevelInfo) const;

	const Image* GetNextImage() const { return m_pNextImage.get(); }

private:
	bool LoadDds(InputStream& inputStream);
	bool LoadStbi(InputStream& inputStream);
	bool LoadExr(InputStream& inputStream);

	int m_Width = 0;
	int m_Height = 0;
	int m_Components = 0;
	int m_Depth = 1;
	int m_MipLevels = 1;
	int m_BBP = 0;
	bool m_sRgb = false;
	bool m_IsArray = false;
	bool m_IsHdr = false;
	std::unique_ptr<Image> m_pNextImage;
	ImageFormat m_Format = ImageFormat::MAX;
	std::vector<unsigned char> m_Pixels;
	std::vector<uint32> m_MipLevelDataOffsets;
};