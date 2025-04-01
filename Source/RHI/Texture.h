#pragma once
#include "DescriptorHandle.h"
#include "DeviceResource.h"

enum class TextureFlag : uint8
{
	None = 0,
	UnorderedAccess = 1 << 0,
	ShaderResource	= 1 << 1,
	RenderTarget	= 1 << 2,
	DepthStencil	= 1 << 3,
	sRGB			= 1 << 4,
};
DECLARE_BITMASK_TYPE(TextureFlag)

enum class TextureType : uint8
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
	uint32				Width				: 16	= 1;
	uint32				Height				: 16	= 1;
	uint32				Depth				: 12	= 1;
	uint32				ArraySize			: 12	= 1;
	uint32				Mips				: 5		= 1;
	uint32				SampleCount			: 3		= 1;
	TextureType			Type						= TextureType::Texture2D;
	ResourceFormat		Format						= ResourceFormat::Unknown;
	TextureFlag			Flags						= TextureFlag::None;
	ClearBinding		ClearBindingValue			= ClearBinding(Colors::Black);

	Vector3u Size() const { return Vector3u(Width, Height, Depth); }
	Vector2u Size2D() const { return Vector2u(Width, Height); }

	bool operator==(const TextureDesc&) const = default;

	static TextureDesc CreateCube(uint32 width, uint32 height, ResourceFormat format, uint32 mips = 1, TextureFlag flags = TextureFlag::None, const ClearBinding& clearBinding = ClearBinding(Colors::Black), uint32 sampleCount = 1)
	{
		gAssert(width);
		gAssert(height);
		TextureDesc desc{};
		desc.Width				= width;
		desc.Height				= height;
		desc.Mips				= mips;
		desc.SampleCount		= sampleCount;
		desc.Format				= format;
		desc.Flags				= flags;
		desc.ClearBindingValue	= clearBinding;
		desc.Type				= TextureType::TextureCube;
		return desc;
	}

	static TextureDesc Create2D(uint32 width, uint32 height, ResourceFormat format, uint32 mips = 1, TextureFlag flags = TextureFlag::None, const ClearBinding& clearBinding = ClearBinding(Colors::Black), uint32 sampleCount = 1)
	{
		gAssert(width);
		gAssert(height);
		TextureDesc desc{};
		desc.Width				= width;
		desc.Height				= height;
		desc.Depth				= 1;
		desc.Mips				= mips;
		desc.SampleCount		= sampleCount;
		desc.Format				= format;
		desc.Flags				= flags;
		desc.ClearBindingValue	= clearBinding;
		desc.Type				= TextureType::Texture2D;
		return desc;
	}

	static TextureDesc Create3D(uint32 width, uint32 height, uint32 depth, ResourceFormat format, uint32 mips = 1, TextureFlag flags = TextureFlag::None, const ClearBinding& clearBinding = ClearBinding(Colors::Black), uint32 sampleCount = 1)
	{
		gAssert(width);
		gAssert(height);
		TextureDesc desc{};
		desc.Width				= width;
		desc.Height				= height;
		desc.Depth				= depth;
		desc.Mips				= mips;
		desc.SampleCount		= sampleCount;
		desc.Format				= format;
		desc.Flags				= flags;
		desc.ClearBindingValue	= clearBinding;
		desc.Type				= TextureType::Texture3D;
		return desc;
	}
	
	bool IsCompatible(const TextureDesc& other) const
	{
		return Width == other.Width
			&& Height == other.Height
			&& Depth == other.Depth
			&& ArraySize == other.ArraySize
			&& Mips == other.Mips
			&& SampleCount == other.SampleCount
			&& Format == other.Format
			&& ClearBindingValue == other.ClearBindingValue
			&& Type == other.Type
			&& EnumHasAllFlags(Flags, other.Flags);
	}
};


struct TextureSRVDesc
{
	TextureSRVDesc(uint8 mipLevel, uint8 numMipLevels)
		: MipLevel(mipLevel), NumMipLevels(numMipLevels)
	{}

	uint8 MipLevel;
	uint8 NumMipLevels;

	bool operator==(const TextureSRVDesc& other) const
	{
		return MipLevel == other.MipLevel &&
			NumMipLevels == other.NumMipLevels;
	}
};


struct TextureUAVDesc
{
	explicit TextureUAVDesc(uint8 mipLevel)
		: MipLevel(mipLevel)
	{}

	uint8 MipLevel;

	bool operator==(const TextureUAVDesc& other) const
	{
		return MipLevel == other.MipLevel;
	}
};



class Texture : public DeviceResource
{
public:
	friend class GraphicsDevice;

	Texture(GraphicsDevice* pParent, const TextureDesc& desc, ID3D12ResourceX* pResource);
	~Texture();

	uint32 GetWidth() const { return m_Desc.Width; }
	uint32 GetHeight() const { return m_Desc.Height; }
	uint32 GetDepth() const { return m_Desc.Depth; }
	uint32 GetArraySize() const { return m_Desc.ArraySize; }
	uint32 GetMipLevels() const { return m_Desc.Mips; }
	ResourceFormat GetFormat() const { return m_Desc.Format; }
	const ClearBinding& GetClearBinding() const { return m_Desc.ClearBindingValue; }
	const TextureDesc& GetDesc() const { return m_Desc; }

	RWTextureView GetUAV(uint32 subresourceIndex = 0) const { gAssert(subresourceIndex < m_UAVs.size()); return m_UAVs[subresourceIndex]; }
	TextureView GetSRV() const { return m_SRV; }

private:
	TextureDesc m_Desc;

	TextureView m_SRV;
	Array<RWTextureView> m_UAVs;
};
