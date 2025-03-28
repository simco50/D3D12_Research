#pragma once
#include "DeviceResource.h"
#include "D3D.h"
#include "DescriptorHandle.h"

enum class BufferFlag : uint8
{
	None = 0,
	UnorderedAccess =		1 << 0,
	ShaderResource =		1 << 1,
	Upload =				1 << 2,
	Readback =				1 << 3,
	ByteAddress =			1 << 4,
	AccelerationStructure = 1 << 5,
	IndirectArguments =		1 << 6,
};
DECLARE_BITMASK_TYPE(BufferFlag)

struct BufferDesc
{
	uint64			Size			= 0;
	uint32			ElementSize		= 1;
	BufferFlag		Flags			= BufferFlag::None;
	ResourceFormat	Format			= ResourceFormat::Unknown;

	bool operator==(const BufferDesc&) const = default;

	static BufferDesc CreateIndexBuffer(uint32 elements, ResourceFormat format, BufferFlag flags = BufferFlag::None)
	{
		gAssert(format == ResourceFormat::R32_UINT || format == ResourceFormat::R16_UINT);
		const FormatInfo& info = RHI::GetFormatInfo(format);
		return { .Size = elements * info.BytesPerBlock, .ElementSize = info.BytesPerBlock, .Flags = flags };
	}

	static BufferDesc CreateVertexBuffer(uint32 elements, uint32 vertexSize, BufferFlag flags = BufferFlag::None)
	{
		return { .Size = elements * vertexSize, .ElementSize = vertexSize, .Flags = flags };
	}

	static BufferDesc CreateReadback(uint64 bytes)
	{
		gAssert(bytes % 4 == 0);
		return { .Size = bytes, .ElementSize = 4, .Flags = BufferFlag::Readback };
	}

	static BufferDesc CreateByteAddress(uint64 bytes, BufferFlag flags = BufferFlag::None)
	{
		gAssert(bytes % 4 == 0);
		return { .Size = bytes, .ElementSize = 4, .Flags = flags | BufferFlag::ShaderResource | BufferFlag::ByteAddress };
	}

	static BufferDesc CreateBLAS(uint64 bytes)
	{
		gAssert(bytes % 4 == 0);
		return { .Size = bytes, .ElementSize = 4, .Flags = BufferFlag::AccelerationStructure | BufferFlag::UnorderedAccess };
	}

	static BufferDesc CreateTLAS(uint64 bytes)
	{
		gAssert(bytes % 4 == 0);
		return { .Size = bytes, .ElementSize = 4, .Flags = BufferFlag::AccelerationStructure | BufferFlag::UnorderedAccess };
	}

	static BufferDesc CreateStructured(uint32 elementCount, uint32 elementSize, BufferFlag flags = BufferFlag::None)
	{
		return { .Size = (uint64)elementCount * elementSize, .ElementSize = elementSize, .Flags = flags | BufferFlag::ShaderResource };
	}

	static BufferDesc CreateTyped(uint32 elementCount, ResourceFormat format, BufferFlag flags = BufferFlag::None)
	{
		const FormatInfo& info = RHI::GetFormatInfo(format);
		gAssert(!info.IsBC);
		return { .Size = (uint64)elementCount * info.BytesPerBlock, .ElementSize = info.BytesPerBlock, .Flags = flags | BufferFlag::ShaderResource, .Format = format };
	}

	template<typename IndirectParameters>
	static BufferDesc CreateIndirectArguments(uint32 elements = 1, BufferFlag flags = BufferFlag::None)
	{
		return { .Size = (uint64)elements * sizeof(IndirectParameters), .ElementSize = sizeof(IndirectParameters), .Flags = flags | BufferFlag::ShaderResource | BufferFlag::IndirectArguments };
	}

	uint32 NumElements() const { return (uint32)(Size / ElementSize); }

	bool IsCompatible(const BufferDesc& rhs) const
	{
		return Size == rhs.Size &&
			ElementSize == rhs.ElementSize &&
			Format == rhs.Format &&
			EnumHasAllFlags(Flags, rhs.Flags);
	}
};


struct BufferUAVDesc
{
	BufferUAVDesc(ResourceFormat format = ResourceFormat::Unknown, bool raw = false, bool counter = false)
		: Format(format), Raw(raw), Counter(counter)
	{}

	static BufferUAVDesc CreateRaw()
	{
		return BufferUAVDesc(ResourceFormat::Unknown, true, false);
	}

	ResourceFormat Format;
	bool Raw;
	bool Counter;
};

struct BufferSRVDesc
{
	BufferSRVDesc(ResourceFormat format = ResourceFormat::Unknown, bool raw = false, uint32 elementOffset = 0, uint32 numElements = 0)
		: Format(format), Raw(raw), ElementOffset(elementOffset), NumElements(numElements)
	{}

	ResourceFormat Format;
	bool Raw;
	uint32 ElementOffset;
	uint32 NumElements;
};



class Buffer : public DeviceResource
{
public:
	friend class GraphicsDevice;

	Buffer(GraphicsDevice* pParent, const BufferDesc& desc, ID3D12ResourceX* pResource);
	~Buffer();

	uint64 GetSize() const { return m_Desc.Size; }
	uint32 GetNumElements() const { return m_Desc.NumElements(); }
	const BufferDesc& GetDesc() const { return m_Desc; }
	UAVHandle GetUAV() const { return m_UAV; }
	SRVHandle GetSRV() const { return m_SRV; }
	void* GetMappedData() const { gAssert(m_pMappedData); return m_pMappedData; }

private:
	UAVHandle m_UAV;
	SRVHandle m_SRV;
	void* m_pMappedData = nullptr;

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
		gAssert(offsetFromStart <= std::numeric_limits<uint32>::max(), "Buffer offset (%llx) will be stored in a 32-bit uint and does not fit.", offsetFromStart);
	}

	VertexBufferView(Buffer* pBuffer)
		: Location(pBuffer->GetGpuHandle()), Elements(pBuffer->GetNumElements()), Stride(pBuffer->GetDesc().ElementSize), OffsetFromStart(0)
	{}

	bool IsValid() const { return Elements > 0; }

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
		gAssert(offsetFromStart <= std::numeric_limits<uint32>::max(), "Buffer offset (%llx) will be stored in a 32-bit uint and does not fit.", offsetFromStart);
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
