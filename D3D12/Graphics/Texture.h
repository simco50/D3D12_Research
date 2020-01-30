#pragma once
#include "GraphicsResource.h"

class Graphics;
class CommandContext;

enum class TextureUsage
{
	None = 0,
	UnorderedAccess = 1 << 0,
	ShaderResource	= 1 << 1,
	RenderTarget	= 1 << 2,
	DepthStencil	= 1 << 3,
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

struct ClearBinding
{
	struct DepthStencilData
	{
		float Depth;
		uint8 Stencil;
	};

	enum class ClearBindingValue
	{
		None,
		Color,
		DepthStencil,
	};

	ClearBinding()
		: BindingValue(ClearBindingValue::None)
	{}

	ClearBinding(const Color& color)
		: BindingValue(ClearBindingValue::Color), Color(color)
	{}

	ClearBinding(float depth, uint8 stencil)
		: BindingValue(ClearBindingValue::DepthStencil)
	{
		DepthStencil.Depth = depth;
		DepthStencil.Stencil = stencil;
	}

	ClearBindingValue BindingValue;
	union
	{
		Color Color;
		DepthStencilData DepthStencil;
	};
};

struct TextureDesc
{
	TextureDesc()
		: Width(1), Height(1), DepthOrArraySize(1), Mips(1), SampleCount(1), Format(DXGI_FORMAT_UNKNOWN), Usage(TextureUsage::None), ClearBindingValue(ClearBinding()), Dimensions(TextureDimension::Texture2D)
	{}

	int Width;
	int Height;
	int DepthOrArraySize;
	int Mips;
	int SampleCount;
	DXGI_FORMAT Format;
	TextureUsage Usage;
	ClearBinding ClearBindingValue;
	TextureDimension Dimensions;

	static TextureDesc Create2D(int width, int height, DXGI_FORMAT format, TextureUsage usage = TextureUsage::ShaderResource, int sampleCount = 1, int mips = 1)
	{
		assert(width);
		assert(height);
		TextureDesc desc;
		desc.Width = width;
		desc.Height = height;
		desc.DepthOrArraySize = 1;
		desc.Mips = mips;
		desc.SampleCount = sampleCount;
		desc.Format = format;
		desc.Usage = usage;
		desc.ClearBindingValue = ClearBinding();
		desc.Dimensions = TextureDimension::Texture2D;
		return desc;
	}

	static TextureDesc CreateDepth(int width, int height, DXGI_FORMAT format, TextureUsage usage = TextureUsage::DepthStencil, int sampleCount = 1, const ClearBinding & clearBinding = ClearBinding(1, 0))
	{
		assert(width);
		assert(height);
		assert((usage & TextureUsage::DepthStencil) == TextureUsage::DepthStencil);
		TextureDesc desc;
		desc.Width = width;
		desc.Height = height;
		desc.DepthOrArraySize = 1;
		desc.Mips = 1;
		desc.SampleCount = sampleCount;
		desc.Format = format;
		desc.Usage = usage;
		desc.ClearBindingValue = clearBinding;
		desc.Dimensions = TextureDimension::Texture2D;
		return desc;
	}

	static TextureDesc CreateRenderTarget(int width, int height, DXGI_FORMAT format, TextureUsage usage = TextureUsage::RenderTarget, int sampleCount = 1, const ClearBinding & clearBinding = ClearBinding(Color(0, 0, 0)))
	{
		assert(width);
		assert(height);
		assert((usage & TextureUsage::RenderTarget) == TextureUsage::RenderTarget);
		TextureDesc desc;
		desc.Width = width;
		desc.Height = height;
		desc.DepthOrArraySize = 1;
		desc.Mips = 1;
		desc.SampleCount = sampleCount;
		desc.Format = format;
		desc.Usage = usage;
		desc.ClearBindingValue = clearBinding;
		desc.Dimensions = TextureDimension::Texture2D;
		return desc;
	}
};

class Texture : public GraphicsResource
{
public:
	Texture();

	void Create(Graphics* pGraphics, const TextureDesc& desc);
	void CreateForSwapchain(Graphics* pGraphics, ID3D12Resource* pTexture);
	void Create(Graphics* pGraphics, CommandContext* pContext, const char* pFilePath);
	void SetData(CommandContext* pContext, const void* pData);

	int GetWidth() const { return m_Desc.Width; }
	int GetHeight() const { return m_Desc.Height; }
	int GetDepth() const { return m_Desc.DepthOrArraySize; }
	int GetArraySize() const { return m_Desc.DepthOrArraySize; }
	int GetMipLevels() const { return m_Desc.Mips; }
	const TextureDesc& GetDesc() const { return m_Desc; }

	D3D12_CPU_DESCRIPTOR_HANDLE GetRTV(int subResource = 0) const;
	D3D12_CPU_DESCRIPTOR_HANDLE GetUAV(int subResource = 0) const;
	D3D12_CPU_DESCRIPTOR_HANDLE GetSRV(int subResource = 0) const;
	D3D12_CPU_DESCRIPTOR_HANDLE GetDSV(bool writeable = true) const;

	DXGI_FORMAT GetFormat() const { return m_Desc.Format; }
	const ClearBinding& GetClearBinding() const { return m_Desc.ClearBindingValue; }

	static int GetRowDataSize(DXGI_FORMAT format, unsigned int width);
	static DXGI_FORMAT GetSrvFormatFromDepth(DXGI_FORMAT format);

private:
	TextureDesc m_Desc;

	//#SimonC: This can hold multiple handles as long as they're sequential in memory. 
	//Need to adapt allocator to work with this nicely so it doesn't waste memory
	CD3DX12_CPU_DESCRIPTOR_HANDLE m_Rtv = {};
	CD3DX12_CPU_DESCRIPTOR_HANDLE m_Uav = {};
	CD3DX12_CPU_DESCRIPTOR_HANDLE m_Srv = {};

	int m_SrvUavDescriptorSize = 0;
	int m_RtvDescriptorSize = 0;
	int m_DsvDescriptorSize = 0;
};