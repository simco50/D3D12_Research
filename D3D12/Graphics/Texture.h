#pragma once
#include "GraphicsResource.h"

class Graphics;
class CommandContext;

enum class TextureUsage
{
	/*Dynamic		= 1 << 0, UNSUPPORTED */
	UnorderedAccess = 1 << 1,
	ShaderResource	= 1 << 2,
	RenderTarget	= 1 << 3,
	DepthStencil	= 1 << 4,
};
DEFINE_ENUM_FLAG_OPERATORS(TextureUsage)

enum class TextureDimension
{
	Texture1D,
	Texture1DArray,
	Texture2D,
	Texture2DArray,
	Texture3D,
	TextureCube,
	TextureCubeArray,
};

class Texture : public GraphicsResource
{
public:
	Texture(ID3D12Device* pDevice);

	int GetWidth() const { return m_Width; }
	int GetHeight() const { return m_Height; }
	int GetDepth() const { return m_DepthOrArraySize; }
	int GetArraySize() const { return m_DepthOrArraySize; }
	int GetMipLevels() const { return m_MipLevels; }

	D3D12_CPU_DESCRIPTOR_HANDLE GetRTV(int subResource = 0) const;
	D3D12_CPU_DESCRIPTOR_HANDLE GetUAV(int subResource = 0) const;
	D3D12_CPU_DESCRIPTOR_HANDLE GetSRV(int subResource = 0) const;
	D3D12_CPU_DESCRIPTOR_HANDLE GetDSV(int subResource = 0) const;

	bool IsArray() const { return m_IsArray; }
	TextureDimension GetDimension() const { return m_Dimension; }

	static int GetRowDataSize(DXGI_FORMAT format, unsigned int width);
	static DXGI_FORMAT GetSrvFormatFromDepth(DXGI_FORMAT format);

protected:
	void Create_Internal(Graphics* pGraphics, TextureDimension dimension, int width, int height, int depthOrArraySize, DXGI_FORMAT format, TextureUsage usage, int sampleCount);
	
	//#SimonC: This can hold multiple handles as long as they're sequential in memory. 
	//Need to adapt allocator to work with this nicely so it doesn't waste memory
	CD3DX12_CPU_DESCRIPTOR_HANDLE m_Rtv = {};
	CD3DX12_CPU_DESCRIPTOR_HANDLE m_Uav = {};
	CD3DX12_CPU_DESCRIPTOR_HANDLE m_Srv = {};
	DXGI_FORMAT m_Format;

	TextureDimension m_Dimension;
	int m_SampleCount = 1;
	int m_Width;
	int m_Height;
	int m_DepthOrArraySize;
	int m_MipLevels;
	bool m_IsArray;

	int m_SrvUavDescriptorSize = 0;
	int m_RtvDescriptorSize = 0;
};

class Texture2D : public Texture
{
public:
	Texture2D(ID3D12Device* pDevice);

	void Create(Graphics* pGraphics, CommandContext* pContext, const char* pFilePath, TextureUsage usage);
	void Create(Graphics* pGraphics, int width, int height, DXGI_FORMAT format, TextureUsage usage, int sampleCount, int arraySize = -1);
	void SetData(CommandContext* pContext, const void* pData);
	void CreateForSwapchain(Graphics* pGraphics, ID3D12Resource* pTexture);
};

class Texture3D : public Texture
{
public:
	Texture3D(ID3D12Device* pDevice);
	void Create(Graphics* pGraphics, int width, int height, int depth, DXGI_FORMAT format, TextureUsage usage);
};

class TextureCube : public Texture 
{
public:
	TextureCube(ID3D12Device* pDevice);
	void Create(Graphics* pGraphics, int width, int height, DXGI_FORMAT format, TextureUsage usage, int sampleCount, int arraySize = -1);
};