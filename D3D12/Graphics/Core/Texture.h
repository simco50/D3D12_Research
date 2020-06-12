#pragma once
#include "GraphicsResource.h"

class Graphics;
class CommandContext;
class UnorderedAccessView;
class ShaderResourceView;
class ResourceView;
class Image;
struct TextureUAVDesc;
struct TextureSRVDesc;

enum class TextureFlag
{
	None = 0,
	/*Dynamic		= 1 << 0, UNSUPPORTED */
	UnorderedAccess = 1 << 1,
	ShaderResource	= 1 << 2,
	RenderTarget	= 1 << 3,
	DepthStencil	= 1 << 4,
};
DECLARE_BITMASK_TYPE(TextureFlag)

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
		DepthStencilData(float depth = 0.0f, uint8 stencil = 1)
			: Depth(depth), Stencil(stencil)
		{}
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
		: BindingValue(ClearBindingValue::None), DepthStencil(DepthStencilData())
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

	bool operator==(const ClearBinding& other) const
	{
		if (BindingValue != other.BindingValue)
		{
			return false;
		}
		if (BindingValue == ClearBindingValue::Color)
		{
			return Color == other.Color;
		}
		return DepthStencil.Depth == other.DepthStencil.Depth
			&& DepthStencil.Stencil == other.DepthStencil.Stencil;
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
		: Width(1), 
		Height(1), 
		DepthOrArraySize(1),
		Mips(1), 
		SampleCount(1), 
		Format(DXGI_FORMAT_UNKNOWN), 
		Usage(TextureFlag::None),
		ClearBindingValue(ClearBinding()),
		Dimensions(TextureDimension::Texture2D)
	{}

	int Width;
	int Height;
	int DepthOrArraySize;
	int Mips;
	int SampleCount;
	DXGI_FORMAT Format;
	TextureFlag Usage;
	ClearBinding ClearBindingValue;
	TextureDimension Dimensions;

	static TextureDesc Create2D(int width, int height, DXGI_FORMAT format, TextureFlag flags = TextureFlag::ShaderResource, int sampleCount = 1, int mips = 1)
	{
		check(width);
		check(height);
		TextureDesc desc;
		desc.Width = width;
		desc.Height = height;
		desc.DepthOrArraySize = 1;
		desc.Mips = mips;
		desc.SampleCount = sampleCount;
		desc.Format = format;
		desc.Usage = flags;
		desc.ClearBindingValue = ClearBinding();
		desc.Dimensions = TextureDimension::Texture2D;
		return desc;
	}

	static TextureDesc CreateDepth(int width, int height, DXGI_FORMAT format, TextureFlag flags = TextureFlag::DepthStencil, int sampleCount = 1, const ClearBinding& clearBinding = ClearBinding(1, 0))
	{
		check(width);
		check(height);
		check(EnumHasAnyFlags(flags, TextureFlag::DepthStencil));
		TextureDesc desc;
		desc.Width = width;
		desc.Height = height;
		desc.DepthOrArraySize = 1;
		desc.Mips = 1;
		desc.SampleCount = sampleCount;
		desc.Format = format;
		desc.Usage = flags;
		desc.ClearBindingValue = clearBinding;
		desc.Dimensions = TextureDimension::Texture2D;
		return desc;
	}

	static TextureDesc CreateRenderTarget(int width, int height, DXGI_FORMAT format, TextureFlag flags = TextureFlag::RenderTarget, int sampleCount = 1, const ClearBinding& clearBinding = ClearBinding(Color(0, 0, 0)))
	{
		check(width);
		check(height);
		check(EnumHasAnyFlags(flags, TextureFlag::RenderTarget));
		TextureDesc desc;
		desc.Width = width;
		desc.Height = height;
		desc.DepthOrArraySize = 1;
		desc.Mips = 1;
		desc.SampleCount = sampleCount;
		desc.Format = format;
		desc.Usage = flags;
		desc.ClearBindingValue = clearBinding;
		desc.Dimensions = TextureDimension::Texture2D;
		return desc;
	}

	bool operator==(const TextureDesc& other) const
	{
		return Width == other.Width
			&& Height == other.Height
			&& DepthOrArraySize == other.DepthOrArraySize
			&& Mips == other.Mips
			&& SampleCount == other.SampleCount
			&& Format == other.Format
			&& Usage == other.Usage
			&& ClearBindingValue == other.ClearBindingValue
			&& Dimensions == other.Dimensions;
	}

	bool operator !=(const TextureDesc& other) const
	{
		return !operator==(other);
	}
};

class Texture : public GraphicsResource
{
public:
	Texture(Graphics* pGraphics, const char* pName = "");
	~Texture();

	void Create(const TextureDesc& desc);
	void CreateForSwapchain(ID3D12Resource* pTexture);
	bool Create(CommandContext* pContext, const char* pFilePath, bool srgb = false);
	bool Create(CommandContext* pContext, const Image& img, bool srgb = false);
	void SetData(CommandContext* pContext, const void* pData);

	int GetWidth() const { return m_Desc.Width; }
	int GetHeight() const { return m_Desc.Height; }
	int GetDepth() const { return m_Desc.DepthOrArraySize; }
	int GetArraySize() const { return m_Desc.DepthOrArraySize; }
	int GetMipLevels() const { return m_Desc.Mips; }
	const TextureDesc& GetDesc() const { return m_Desc; }

	void CreateUAV(UnorderedAccessView** pView, const TextureUAVDesc& desc);
	void CreateSRV(ShaderResourceView** pView, const TextureSRVDesc& desc);

	D3D12_CPU_DESCRIPTOR_HANDLE GetRTV() const;
	D3D12_CPU_DESCRIPTOR_HANDLE GetUAV() const;
	D3D12_CPU_DESCRIPTOR_HANDLE GetSRV() const;
	D3D12_CPU_DESCRIPTOR_HANDLE GetDSV(bool writeable = true) const;

	DXGI_FORMAT GetFormat() const { return m_Desc.Format; }
	const ClearBinding& GetClearBinding() const { return m_Desc.ClearBindingValue; }

	static DXGI_FORMAT GetSrvFormat(DXGI_FORMAT format);

private:
	TextureDesc m_Desc;

	//#SimonC: This can hold multiple handles as long as they're sequential in memory. 
	//Need to adapt allocator to work with this nicely so it doesn't waste memory
	CD3DX12_CPU_DESCRIPTOR_HANDLE m_Rtv = {};
	CD3DX12_CPU_DESCRIPTOR_HANDLE m_ReadOnlyDsv = {};

	ShaderResourceView* m_pSrv = nullptr;
	UnorderedAccessView* m_pUav = nullptr;

	std::string m_Name;
};