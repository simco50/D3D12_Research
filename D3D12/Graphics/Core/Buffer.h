#pragma once
#include "GraphicsResource.h"
class CommandContext;
class Buffer;
class ShaderResourceView;
class UnorderedAccessView;
class ResourceView;
struct BufferSRVDesc;
struct BufferUAVDesc;

enum class BufferFlag
{
	None = 0,
	UnorderedAccess =		1 << 0,
	ShaderResource =		1 << 1,
	Upload =				1 << 2,
	Readback =				1 << 3,
	Structured =			1 << 4,
	ByteAddress =			1 << 5,
	IndirectArguments =		1 << 6,
	AccelerationStructure = 1 << 7,
};
DECLARE_BITMASK_TYPE(BufferFlag)

struct BufferDesc
{
	BufferDesc() = default;
	BufferDesc(uint32 elements, uint32 elementSize, BufferFlag usage = BufferFlag::None)
		: Size((uint64)elements * elementSize), ElementSize(elementSize), Usage(usage)
	{}

	static BufferDesc CreateBuffer(uint64 sizeInBytes, BufferFlag usage = BufferFlag::None)
	{
		BufferDesc desc;
		desc.Size = sizeInBytes;
		desc.ElementSize = 1;
		desc.Usage = usage;
		return desc;
	}

	static BufferDesc CreateIndexBuffer(uint32 elements, bool smallIndices, BufferFlag usage = BufferFlag::None)
	{
		return BufferDesc(elements, smallIndices ? 2 : 4, usage);
	}

	static BufferDesc CreateVertexBuffer(uint32 elements, uint32 vertexSize, BufferFlag usage = BufferFlag::None)
	{
		return BufferDesc(elements, vertexSize, usage);
	}

	static BufferDesc CreateReadback(uint64 size)
	{
		return CreateBuffer(size, BufferFlag::Readback);
	}

	static BufferDesc CreateByteAddress(uint64 bytes, BufferFlag usage = BufferFlag::ShaderResource)
	{
		check(bytes % 4 == 0);
		BufferDesc desc;
		desc.Size = bytes;
		desc.ElementSize = 4;
		desc.Usage = usage | BufferFlag::ByteAddress | BufferFlag::UnorderedAccess;
		return desc;
	}

	static BufferDesc CreateAccelerationStructure(uint64 bytes)
	{
		check(bytes % 4 == 0);
		BufferDesc desc;
		desc.Size = bytes;
		desc.ElementSize = 4;
		desc.Usage = desc.Usage | BufferFlag::AccelerationStructure | BufferFlag::UnorderedAccess;
		return desc;
	}

	static BufferDesc CreateStructured(uint32 elementCount, uint32 elementSize, BufferFlag usage = BufferFlag::ShaderResource | BufferFlag::UnorderedAccess)
	{
		BufferDesc desc;
		desc.ElementSize = elementSize;
		desc.Size = (uint64)elementCount * desc.ElementSize;
		desc.Usage = usage | BufferFlag::Structured;
		return desc;
	}

	static BufferDesc CreateTyped(uint32 elementCount, DXGI_FORMAT format, BufferFlag usage = BufferFlag::ShaderResource | BufferFlag::UnorderedAccess)
	{
		check(!D3D::IsBlockCompressFormat(format));
		BufferDesc desc;
		desc.ElementSize = D3D::GetFormatRowDataSize(format, 1);
		desc.Size = (uint64)elementCount * desc.ElementSize;
		desc.Format = format;
		desc.Usage = usage;
		return desc;
	}

	template<typename IndirectParameters>
	static BufferDesc CreateIndirectArguments(uint32 elements = 1, BufferFlag usage = BufferFlag::None)
	{
		BufferDesc desc;
		desc.ElementSize = sizeof(IndirectParameters);
		desc.Size = (uint64)elements * desc.ElementSize;
		desc.Usage = usage | BufferFlag::IndirectArguments | BufferFlag::UnorderedAccess;
		return desc;
	}

	uint32 NumElements() const { return (uint32)(Size / ElementSize); }

	bool operator==(const BufferDesc& other) const
	{
		return Size == other.Size
			&& ElementSize == other.ElementSize
			&& Usage == other.Usage;
	}

	bool operator!=(const BufferDesc& other) const
	{
		return Size != other.Size
			|| ElementSize != other.ElementSize
			|| Usage != other.Usage;
	}

	uint64 Size = 0;
	uint32 ElementSize = 0;
	BufferFlag Usage = BufferFlag::None;
	DXGI_FORMAT Format = DXGI_FORMAT_UNKNOWN;
};

class Buffer : public GraphicsResource
{
public:
	Buffer(GraphicsDevice* pParent, const char* pName = "");
	Buffer(GraphicsDevice* pParent, const BufferDesc& desc, const char* pName = "");
	Buffer(GraphicsDevice* pParent, ID3D12Resource * pResource, D3D12_RESOURCE_STATES state);
	~Buffer();
	void Create(const BufferDesc& desc);

	inline uint64 GetSize() const { return m_Desc.Size; }
	inline uint32 GetNumElements() const { return m_Desc.NumElements(); }
	inline const BufferDesc& GetDesc() const { return m_Desc; }

	void CreateUAV(UnorderedAccessView** pView, const BufferUAVDesc& desc);
	void CreateSRV(ShaderResourceView** pView, const BufferSRVDesc& desc);

	ShaderResourceView* GetSRV() const { return m_pSrv; };
	UnorderedAccessView* GetUAV() const { return m_pUav; };

	int32 GetSRVIndex() const;
	int32 GetUAVIndex() const;

protected:
	UnorderedAccessView* m_pUav = nullptr;
	ShaderResourceView* m_pSrv = nullptr;

	BufferDesc m_Desc;
};

struct VertexBufferView
{
	VertexBufferView()
		: Location(~0u), Elements(0), Stride(0)
	{}
	VertexBufferView(D3D12_GPU_VIRTUAL_ADDRESS location, uint32 elements, uint32 stride)
		: Location(location), Elements(elements), Stride(stride)
	{}

	D3D12_GPU_VIRTUAL_ADDRESS Location;
	uint32 Elements;
	uint32 Stride;
};

struct IndexBufferView
{
	IndexBufferView()
		: Location(~0u), Elements(0), Format(DXGI_FORMAT_R32_UINT)
	{}
	IndexBufferView(D3D12_GPU_VIRTUAL_ADDRESS location, uint32 elements, DXGI_FORMAT format = DXGI_FORMAT_R32_UINT)
		: Location(location), Elements(elements), Format(format)
	{}

	uint32 Stride() const
	{
		return D3D::GetFormatRowDataSize(Format, 1);
	}

	D3D12_GPU_VIRTUAL_ADDRESS Location;
	uint32 Elements;
	DXGI_FORMAT Format;
};