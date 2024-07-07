#pragma once

class CommandQueue;
class GraphicsDevice;
class PipelineState;
class StateObject;
class Texture;
struct TextureDesc;
class Buffer;
struct BufferDesc;
class RootSignature;
class CommandSignature;
class CommandContext;
class ShaderBindingTable;
class ResourceView;
class ShaderResourceView;
class UnorderedAccessView;
class PipelineStateInitializer;
class StateObjectInitializer;

enum class ResourceFormat : uint8
{
	Unknown,

	R8_UINT,
	R8_SINT,
	R8_UNORM,
	R8_SNORM,
	RG8_UINT,
	RG8_SINT,
	RG8_UNORM,
	RG8_SNORM,
	R16_UINT,
	R16_SINT,
	R16_UNORM,
	R16_SNORM,
	R16_FLOAT,
	BGRA4_UNORM,
	B5G6R5_UNORM,
	B5G5R5A1_UNORM,
	RGBA8_UINT,
	RGBA8_SINT,
	RGBA8_UNORM,
	RGBA8_SNORM,
	BGRA8_UNORM,
	RGB10A2_UNORM,
	R11G11B10_FLOAT,
	RG16_UINT,
	RG16_SINT,
	RG16_UNORM,
	RG16_SNORM,
	RG16_FLOAT,
	R32_UINT,
	R32_SINT,
	R32_FLOAT,
	RGBA16_UINT,
	RGBA16_SINT,
	RGBA16_FLOAT,
	RGBA16_UNORM,
	RGBA16_SNORM,
	RG32_UINT,
	RG32_SINT,
	RG32_FLOAT,
	RGB32_UINT,
	RGB32_SINT,
	RGB32_FLOAT,
	RGBA32_UINT,
	RGBA32_SINT,
	RGBA32_FLOAT,

	BC1_UNORM,
	BC2_UNORM,
	BC3_UNORM,
	BC4_UNORM,
	BC4_SNORM,
	BC5_UNORM,
	BC5_SNORM,
	BC6H_UFLOAT,
	BC6H_SFLOAT,
	BC7_UNORM,

	D16_UNORM,
	D32_FLOAT,
	D24S8,
	D32S8,

	Num,
};

enum class FormatType : uint8
{
	Integer,
	Normalized,
	Float,
	DepthStencil
};

struct FormatInfo
{
	const char*		pName;
	ResourceFormat	Format;
	FormatType		Type;
	uint8			BytesPerBlock	: 8;
	uint8			BlockSize		: 4;
	uint8			NumComponents	: 3;
	uint8			IsDepth			: 1;
	uint8			IsStencil		: 1;
	uint8			IsSigned		: 1;
	uint8			IsBC			: 1;
};

namespace RHI
{
	const FormatInfo& GetFormatInfo(ResourceFormat format);

	uint64 GetRowPitch(ResourceFormat format, uint32 width, uint32 mipIndex = 0);
	uint64 GetSlicePitch(ResourceFormat format, uint32 width, uint32 height, uint32 mipIndex = 0);
	uint64 GetTextureMipByteSize(ResourceFormat format, uint32 width, uint32 height, uint32 depth, uint32 mipIndex);
	uint64 GetTextureByteSize(ResourceFormat format, uint32 width, uint32 height, uint32 depth = 1, uint32 numMips = 1);
}

struct FreeList
{
public:
	FreeList(uint32 size)
	{
		m_FreeList.resize(size);
		std::iota(m_FreeList.begin(), m_FreeList.end(), 0);
	}

	~FreeList()
	{
		check(m_NumAllocations == 0, "Free list not fully released");
	}

	uint32 Allocate()
	{
		uint32 slot = m_NumAllocations++;
		check(slot < m_FreeList.size());
		return m_FreeList[slot];
	}

	void Free(uint32 index)
	{
		uint32 freed_index = m_NumAllocations--;
		check(freed_index > 0);
		m_FreeList[freed_index - 1] = index;
	}

	bool CanAllocate() const { return m_NumAllocations < m_FreeList.size(); }

private:
	Array<uint32> m_FreeList;
	uint32 m_NumAllocations = 0;
};
