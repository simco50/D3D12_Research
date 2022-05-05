#pragma once
#include "GraphicsResource.h"

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

	uint32 Width;
	uint32 Height;
	uint32 DepthOrArraySize;
	uint32 Mips;
	uint32 SampleCount;
	DXGI_FORMAT Format;
	TextureFlag Usage;
	ClearBinding ClearBindingValue;
	TextureDimension Dimensions;

	IntVector3 Size() const { return IntVector3(Width, Height, DepthOrArraySize); }

	static TextureDesc CreateCube(uint32 width, uint32 height, DXGI_FORMAT format, TextureFlag flags = TextureFlag::None, uint32 sampleCount = 1, uint32 mips = 1)
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
		desc.Usage = flags | TextureFlag::ShaderResource;
		desc.ClearBindingValue = ClearBinding();
		desc.Dimensions = TextureDimension::TextureCube;
		return desc;
	}

	static TextureDesc Create2D(uint32 width, uint32 height, DXGI_FORMAT format, TextureFlag flags = TextureFlag::None, uint32 sampleCount = 1, uint32 mips = 1)
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
		desc.Usage = flags | TextureFlag::ShaderResource;
		desc.ClearBindingValue = ClearBinding();
		desc.Dimensions = TextureDimension::Texture2D;
		return desc;
	}

	static TextureDesc Create3D(uint32 width, uint32 height, uint32 depth, DXGI_FORMAT format, TextureFlag flags = TextureFlag::None, uint32 sampleCount = 1, uint32 mips = 1)
	{
		check(width);
		check(height);
		TextureDesc desc;
		desc.Width = width;
		desc.Height = height;
		desc.DepthOrArraySize = depth;
		desc.Mips = mips;
		desc.SampleCount = sampleCount;
		desc.Format = format;
		desc.Usage = flags | TextureFlag::ShaderResource;
		desc.ClearBindingValue = ClearBinding();
		desc.Dimensions = TextureDimension::Texture3D;
		return desc;
	}

	static TextureDesc CreateDepth(uint32 width, uint32 height, DXGI_FORMAT format, TextureFlag flags = TextureFlag::None, uint32 sampleCount = 1, const ClearBinding& clearBinding = ClearBinding(1, 0))
	{
		check(width);
		check(height);
		TextureDesc desc;
		desc.Width = width;
		desc.Height = height;
		desc.DepthOrArraySize = 1;
		desc.Mips = 1;
		desc.SampleCount = sampleCount;
		desc.Format = format;
		desc.Usage = flags | TextureFlag::DepthStencil;
		desc.ClearBindingValue = clearBinding;
		desc.Dimensions = TextureDimension::Texture2D;
		return desc;
	}

	static TextureDesc CreateRenderTarget(uint32 width, uint32 height, DXGI_FORMAT format, TextureFlag flags = TextureFlag::None, uint32 sampleCount = 1, const ClearBinding& clearBinding = ClearBinding(Color(0, 0, 0)))
	{
		check(width);
		check(height);
		TextureDesc desc;
		desc.Width = width;
		desc.Height = height;
		desc.DepthOrArraySize = 1;
		desc.Mips = 1;
		desc.SampleCount = sampleCount;
		desc.Format = format;
		desc.Usage = flags | TextureFlag::RenderTarget;
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

	bool operator!=(const TextureDesc& other) const
	{
		return !operator==(other);
	}
};

class Texture : public GraphicsResource
{
public:
	friend class GraphicsDevice;

	Texture(GraphicsDevice* pParent, const TextureDesc& desc, ID3D12Resource* pResource);

	uint32 GetWidth() const { return m_Desc.Width; }
	uint32 GetHeight() const { return m_Desc.Height; }
	uint32 GetDepth() const { return m_Desc.DepthOrArraySize; }
	uint32 GetArraySize() const { return m_Desc.DepthOrArraySize; }
	uint32 GetMipLevels() const { return m_Desc.Mips; }
	IntVector3 GetSize() const { return m_Desc.Size(); }
	DXGI_FORMAT GetFormat() const { return m_Desc.Format; }
	const ClearBinding& GetClearBinding() const { return m_Desc.ClearBindingValue; }
	const TextureDesc& GetDesc() const { return m_Desc; }

	D3D12_CPU_DESCRIPTOR_HANDLE GetRTV() const;
	D3D12_CPU_DESCRIPTOR_HANDLE GetDSV(bool writeable = true) const;

private:

	TextureDesc m_Desc;

	D3D12_CPU_DESCRIPTOR_HANDLE m_Rtv = {};
	D3D12_CPU_DESCRIPTOR_HANDLE m_ReadOnlyDsv = {};
};
