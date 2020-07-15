#pragma once

enum class ImageFormat
{
	RGBA = 0,
	BGRA,
	RGB32,
	RGBA16,
	RGBA32,
	BC1,
	BC2,
	BC3,
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

class Image final
{
public:
	bool Load(const char* filePath);
	bool Load(const void* pPixels, size_t dataSize, const char* pFormatHint);
	void Save(const char* pFilePath);

	bool SetSize(int x, int y, int components);
	bool SetData(const void* pPixels);
	bool SetData(const void* pData, uint32 offsetInBytes, uint32 sizeInBytes);
	bool SetPixel(int x, int y, const Color& color);
	bool SetPixelInt(int x, int y, unsigned int color);

	Color GetPixel(int x, int y) const;
	unsigned int GetPixelInt(int x, int y) const;

	int GetWidth() const { return m_Width; }
	int GetHeight() const { return m_Height; }
	int GetDepth() const { return m_Depth; }
	int GetComponents() const { return m_Components; }
	bool IsSRGB() const { return m_sRgb; }
	bool IsHDR() const { return m_IsHdr; }
	bool IsCubemap() const { return m_IsCubemap; }

	unsigned char* GetWritableData() { return m_Pixels.data(); }
	const unsigned char* GetData(int mipLevel = 0) const;
	uint32 GetTotalSize() const { return (uint32)m_Pixels.size(); }

	MipLevelInfo GetMipInfo(int mipLevel) const;
	int GetMipLevels() const { return m_MipLevels; }
	bool IsCompressed() const { return m_Format != ImageFormat::RGBA; }
	ImageFormat GetFormat() const { return m_Format; }
	bool GetSurfaceInfo(int width, int height, int depth, int mipLevel, MipLevelInfo& mipLevelInfo) const;

	const Image* GetNextImage() const { return m_pNextImage.get(); }

	static unsigned int TextureFormatFromCompressionFormat(const ImageFormat& format, bool sRgb);

private:
	bool LoadDds(const char* inputStream);
	bool LoadStbi(const char* inputStream);

	int m_Width = 0;
	int m_Height = 0;
	int m_Components = 0;
	int m_Depth = 1;
	int m_MipLevels = 1;
	int m_BBP = 0;
	bool m_sRgb = false;
	bool m_IsArray = false;
	bool m_IsHdr = false;
	bool m_IsCubemap = false;
	std::unique_ptr<Image> m_pNextImage;
	ImageFormat m_Format = ImageFormat::MAX;
	std::array<uint32, D3D12_REQ_MIP_LEVELS> m_MipLevelDataOffsets{};
	std::vector<unsigned char> m_Pixels;
};