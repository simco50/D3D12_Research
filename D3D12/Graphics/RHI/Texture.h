#pragma once
#include "GraphicsResource.h"

enum class TextureFlag
{
	None = 0,
	UnorderedAccess = 1 << 0,
	ShaderResource	= 1 << 1,
	RenderTarget	= 1 << 2,
	DepthStencil	= 1 << 3,
	sRGB			= 1 << 4,
};
DECLARE_BITMASK_TYPE(TextureFlag)

enum class TextureType
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
	TextureDesc() = default;

	uint32 Width						= 1;
	uint32 Height						= 1;
	uint32 DepthOrArraySize				= 1;
	uint32 Mips							= 1;
	TextureType Type					= TextureType::Texture2D;
	uint32 SampleCount					= 1;
	ResourceFormat Format				= ResourceFormat::Unknown;
	TextureFlag Usage					= TextureFlag::None;
	ClearBinding ClearBindingValue		= ClearBinding(Colors::Black);

	Vector3u Size() const { return Vector3u(Width, Height, DepthOrArraySize); }
	Vector2u Size2D() const { return Vector2u(Width, Height); }

	static TextureDesc CreateCube(uint32 width, uint32 height, ResourceFormat format, uint32 mips = 1, TextureFlag flags = TextureFlag::None, const ClearBinding& clearBinding = ClearBinding(Colors::Black), uint32 sampleCount = 1)
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
		desc.ClearBindingValue = clearBinding;
		desc.Type = TextureType::TextureCube;
		return desc;
	}

	static TextureDesc Create2D(uint32 width, uint32 height, ResourceFormat format, uint32 mips = 1, TextureFlag flags = TextureFlag::None, const ClearBinding& clearBinding = ClearBinding(Colors::Black), uint32 sampleCount = 1)
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
		desc.ClearBindingValue = clearBinding;
		desc.Type = TextureType::Texture2D;
		return desc;
	}

	static TextureDesc Create3D(uint32 width, uint32 height, uint32 depth, ResourceFormat format, uint32 mips = 1, TextureFlag flags = TextureFlag::None, const ClearBinding& clearBinding = ClearBinding(Colors::Black), uint32 sampleCount = 1)
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
		desc.Usage = flags;
		desc.ClearBindingValue = clearBinding;
		desc.Type = TextureType::Texture3D;
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
			&& Type == other.Type;
	}

	bool IsCompatible(const TextureDesc& other) const
	{
		return Width == other.Width
			&& Height == other.Height
			&& DepthOrArraySize == other.DepthOrArraySize
			&& Mips == other.Mips
			&& SampleCount == other.SampleCount
			&& Format == other.Format
			&& ClearBindingValue == other.ClearBindingValue
			&& Type == other.Type
			&& EnumHasAllFlags(Usage, other.Usage);
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
	~Texture();

	uint32 GetWidth() const { return m_Desc.Width; }
	uint32 GetHeight() const { return m_Desc.Height; }
	uint32 GetDepth() const { return m_Desc.DepthOrArraySize; }
	uint32 GetArraySize() const { return m_Desc.DepthOrArraySize; }
	uint32 GetMipLevels() const { return m_Desc.Mips; }
	ResourceFormat GetFormat() const { return m_Desc.Format; }
	const ClearBinding& GetClearBinding() const { return m_Desc.ClearBindingValue; }
	const TextureDesc& GetDesc() const { return m_Desc; }

	D3D12_CPU_DESCRIPTOR_HANDLE GetRTV() const;
	D3D12_CPU_DESCRIPTOR_HANDLE GetDSV(bool writeable = true) const;
	UnorderedAccessView* GetUAV(uint32 subresourceIndex = 0) const;
	ShaderResourceView* GetSRV() const { return m_pSRV; }
	uint32 GetUAVIndex(uint32 subresourceIndex = 0) const;
	uint32 GetSRVIndex() const;

private:
	TextureDesc m_Desc;
	
	RefCountPtr<ShaderResourceView> m_pSRV;
	std::vector<RefCountPtr<UnorderedAccessView>> m_UAVs;

	D3D12_CPU_DESCRIPTOR_HANDLE m_RTV = {};
	D3D12_CPU_DESCRIPTOR_HANDLE m_ReadOnlyDSV = {};
};
