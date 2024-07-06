#pragma once
#include "Graphics/RHI/RHI.h"

class Stream;

class Image final
{
public:
	Image(ResourceFormat format = ResourceFormat::Unknown);
	Image(uint32 width, uint32 height, uint32 depth, ResourceFormat format, uint32 numMips = 1, const void* pInitialData = nullptr);
	bool Load(const char* filePath);
	bool Load(Stream& stream, const char* pFormatHint);
	void Save(const char* pFilePath);

	bool SetSize(uint32 x, uint32 y, uint32 depth, uint32 numMips);
	bool SetData(const void* pPixels);
	bool SetData(const void* pData, uint32 offsetInBytes, uint32 sizeInBytes);
	bool SetPixel(uint32 x, uint32 y, const Color& color);
	bool SetPixelInt(uint32 x, uint32 y, unsigned int color);

	Color GetPixel(uint32 x, uint32 y) const;
	uint32 GetPixelInt(uint32 x, uint32 y) const;

	uint32 GetWidth() const { return m_Width; }
	uint32 GetHeight() const { return m_Height; }
	uint32 GetDepth() const { return m_Depth; }
	bool IsSRGB() const { return m_sRgb; }
	bool IsHDR() const { return m_IsHdr; }
	bool IsCubemap() const { return m_IsCubemap; }

	const unsigned char* GetData(uint32 mipLevel = 0) const;

	uint32 GetMipLevels() const { return m_MipLevels; }
	ResourceFormat GetFormat() const { return m_Format; }
	const Image* GetNextImage() const { return m_pNextImage.get(); }

private:
	bool LoadDDS(Stream& stream);
	bool LoadSTB(Stream& stream);

	uint32 m_Width = 0;
	uint32 m_Height = 0;
	uint32 m_Depth = 1;
	uint32 m_MipLevels = 1;
	bool m_sRgb = false;
	bool m_IsArray = false;
	bool m_IsHdr = false;
	bool m_IsCubemap = false;
	std::unique_ptr<Image> m_pNextImage;
	ResourceFormat m_Format = ResourceFormat::Unknown;
	StaticArray<uint64, D3D12_REQ_MIP_LEVELS> m_MipLevelDataOffsets{};
	Array<uint8> m_Pixels;
};
