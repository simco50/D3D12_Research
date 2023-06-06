#pragma once
#include "GraphicsResource.h"
#include "D3D.h"

enum class BufferFlag
{
	None = 0,
	UnorderedAccess =		1 << 0,
	ShaderResource =		1 << 1,
	Upload =				1 << 2,
	Readback =				1 << 3,
	ByteAddress =			1 << 4,
	AccelerationStructure = 1 << 5,
	IndirectArguments =		1 << 6,
	NoBindless =			1 << 7,
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
		return CreateBuffer(size, BufferFlag::Readback | BufferFlag::NoBindless);
	}

	static BufferDesc CreateByteAddress(uint64 bytes, BufferFlag usage = BufferFlag::None)
	{
		check(bytes % 4 == 0);
		BufferDesc desc;
		desc.Size = bytes;
		desc.ElementSize = 4;
		desc.Usage = usage | BufferFlag::ShaderResource | BufferFlag::ByteAddress;
		return desc;
	}

	static BufferDesc CreateBLAS(uint64 bytes)
	{
		check(bytes % 4 == 0);
		BufferDesc desc;
		desc.Size = bytes;
		desc.ElementSize = 4;
		desc.Usage = desc.Usage | BufferFlag::AccelerationStructure | BufferFlag::UnorderedAccess | BufferFlag::NoBindless;
		return desc;
	}

	static BufferDesc CreateTLAS(uint64 bytes)
	{
		check(bytes % 4 == 0);
		BufferDesc desc;
		desc.Size = bytes;
		desc.ElementSize = 4;
		desc.Usage = desc.Usage | BufferFlag::AccelerationStructure | BufferFlag::UnorderedAccess;
		return desc;
	}

	static BufferDesc CreateStructured(uint32 elementCount, uint32 elementSize, BufferFlag usage = BufferFlag::None)
	{
		BufferDesc desc;
		desc.ElementSize = elementSize;
		desc.Size = (uint64)elementCount * desc.ElementSize;
		desc.Usage = usage | BufferFlag::ShaderResource;
		return desc;
	}

	static BufferDesc CreateTyped(uint32 elementCount, ResourceFormat format, BufferFlag usage = BufferFlag::None)
	{
		const FormatInfo& info = RHI::GetFormatInfo(format);
		check(!info.IsBC);
		BufferDesc desc;
		desc.ElementSize = info.BytesPerBlock;
		desc.Size = (uint64)elementCount * desc.ElementSize;
		desc.Format = format;
		desc.Usage = usage | BufferFlag::ShaderResource;
		return desc;
	}

	template<typename IndirectParameters>
	static BufferDesc CreateIndirectArguments(uint32 elements = 1, BufferFlag usage = BufferFlag::None)
	{
		BufferDesc desc;
		desc.ElementSize = sizeof(IndirectParameters);
		desc.Size = (uint64)elements * desc.ElementSize;
		desc.Usage = usage | BufferFlag::ShaderResource | BufferFlag::IndirectArguments;
		return desc;
	}

	uint32 NumElements() const { return (uint32)(Size / ElementSize); }

	bool operator==(const BufferDesc& rhs) const
	{
		return Size == rhs.Size &&
			ElementSize == rhs.ElementSize &&
			Usage == rhs.Usage &&
			Format == rhs.Format;
	}

	bool IsCompatible(const BufferDesc& rhs) const
	{
		return Size == rhs.Size &&
			ElementSize == rhs.ElementSize &&
			Format == rhs.Format &&
			EnumHasAllFlags(Usage, rhs.Usage);
	}

	uint64 Size = 0;
	uint32 ElementSize = 0;
	BufferFlag Usage = BufferFlag::None;
	ResourceFormat Format = ResourceFormat::Unknown;
};

class Buffer : public GraphicsResource
{
public:
	Buffer(GraphicsDevice* pParent, const BufferDesc& desc, ID3D12Resource* pResource);

	inline uint64 GetSize() const { return m_Desc.Size; }
	inline uint32 GetNumElements() const { return m_Desc.NumElements(); }
	inline const BufferDesc& GetDesc() const { return m_Desc; }

private:
	const BufferDesc m_Desc;
};

struct VertexBufferView
{
	VertexBufferView()
		: Location(~0ull), Elements(0), Stride(0), OffsetFromStart(~0u)
	{}

	VertexBufferView(D3D12_GPU_VIRTUAL_ADDRESS location, uint32 elements, uint32 stride, uint64 offsetFromStart)
		: Location(location), Elements(elements), Stride(stride), OffsetFromStart((uint32)offsetFromStart)
	{
		checkf(offsetFromStart <= std::numeric_limits<uint32>::max(), "Buffer offset (%llx) will be stored in a 32-bit uint and does not fit.", offsetFromStart);
	}

	VertexBufferView(Buffer* pBuffer)
		: Location(pBuffer->GetGpuHandle()), Elements(pBuffer->GetNumElements()), Stride(pBuffer->GetDesc().ElementSize), OffsetFromStart(0)
	{}

	D3D12_GPU_VIRTUAL_ADDRESS Location;
	uint32 Elements;
	uint32 Stride;
	uint32 OffsetFromStart;
};

struct IndexBufferView
{
	IndexBufferView()
		: Location(~0ull), Elements(0), OffsetFromStart(0), Format(ResourceFormat::R32_UINT)
	{}

	IndexBufferView(D3D12_GPU_VIRTUAL_ADDRESS location, uint32 elements, ResourceFormat format, uint64 offsetFromStart)
		: Location(location), Elements(elements), OffsetFromStart((uint32)offsetFromStart), Format(format)
	{
		checkf(offsetFromStart <= std::numeric_limits<uint32>::max(), "Buffer offset (%llx) will be stored in a 32-bit uint and does not fit.", offsetFromStart);
	}

	uint32 Stride() const
	{
		return RHI::GetFormatInfo(Format).BytesPerBlock;
	}

	D3D12_GPU_VIRTUAL_ADDRESS Location;
	uint32 Elements;
	uint32 OffsetFromStart;
	ResourceFormat Format;
};
