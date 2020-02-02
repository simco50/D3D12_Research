#pragma once
#include "GraphicsResource.h"
class CommandContext;
class Graphics;
class Buffer;

enum class BufferFlag
{
	None				= 0,
	UnorderedAccess		= 1 << 0,
	ShaderResource		= 1 << 1,
	Upload				= 1 << 2,
	Readback			= 1 << 3,
	Structured			= 1 << 4,
	ByteAddress			= 1 << 5,
	IndirectArguments	= 1 << 6,
};
DECLARE_BITMASK_TYPE(BufferFlag)

struct BufferDesc
{
	BufferDesc() = default;
	BufferDesc(int elements, int elementSize, BufferFlag usage = BufferFlag::None)
		: ElementCount(elements), ElementSize(elementSize), Usage(usage)
	{}

	static BufferDesc CreateIndexBuffer(int elements, bool smallIndices, BufferFlag usage = BufferFlag::None)
	{
		return BufferDesc(elements, smallIndices ? 2 : 4, usage);
	}

	static BufferDesc CreateVertexBuffer(int elements, int vertexSize, BufferFlag usage = BufferFlag::None)
	{
		return BufferDesc(elements, vertexSize, usage);
	}

	static BufferDesc CreateReadback(int size)
	{
		return BufferDesc(size, sizeof(uint64), BufferFlag::Readback);
	}

	static BufferDesc CreateByteAddress(int bytes, BufferFlag usage = BufferFlag::ShaderResource | BufferFlag::UnorderedAccess)
	{
		assert(bytes % 4 == 0);
		BufferDesc desc;
		desc.ElementCount = bytes / 4;
		desc.ElementSize = 4;
		desc.Usage = usage | BufferFlag::ByteAddress;
		return desc;
	}

	static BufferDesc CreateStructured(int elementCount, int elementSize, BufferFlag usage = BufferFlag::ShaderResource | BufferFlag::UnorderedAccess)
	{
		BufferDesc desc;
		desc.ElementCount = elementCount;
		desc.ElementSize = elementSize;
		desc.Usage = usage | BufferFlag::Structured;
		return desc;
	}

	template<typename IndirectParameters>
	static BufferDesc CreateIndirectArguments(int elements = 1, BufferFlag usage = BufferFlag::IndirectArguments | BufferFlag::UnorderedAccess)
	{
		BufferDesc desc;
		desc.ElementCount = elements;
		desc.ElementSize = size(IndirectParameters);
		desc.Usage = usage | BufferFlag::IndirectArguments;
		return desc;
	}

	bool operator==(const BufferDesc& other) const
	{
		return ElementCount == other.ElementCount
			&& ElementSize == other.ElementSize
			&& Usage == other.Usage;
	}

	bool operator!=(const BufferDesc& other) const
	{
		return ElementCount != other.ElementCount
			|| ElementSize != other.ElementSize
			|| Usage != other.Usage;
	}

	int ElementCount = 0;
	int ElementSize = 0;
	BufferFlag Usage = BufferFlag::None;
};

struct BufferUAVDesc
{
	static BufferUAVDesc CreateStructured(Buffer* pCounter = nullptr)
	{
		BufferUAVDesc desc;
		desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.FirstElement = 0;
		desc.CounterOffset = 0;
		desc.pCounter = pCounter;
		return desc;
	}

	static BufferUAVDesc CreateTyped(DXGI_FORMAT format, Buffer* pCounter = nullptr)
	{
		BufferUAVDesc desc;
		desc.Format = format;
		desc.FirstElement = 0;
		desc.CounterOffset = 0;
		desc.pCounter = pCounter;
		return desc;
	}

	static BufferUAVDesc CreateByteAddress()
	{
		BufferUAVDesc desc;
		desc.Format = DXGI_FORMAT_R32_TYPELESS;
		desc.FirstElement = 0;
		desc.CounterOffset = 0;
		desc.pCounter = nullptr;
		return desc;
	}

	DXGI_FORMAT Format;
	int FirstElement;
	int CounterOffset;
	Buffer* pCounter;
};

struct BufferSRVDesc
{
	static BufferSRVDesc CreateStructured(Buffer* pCounter = nullptr)
	{
		BufferSRVDesc desc;
		desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.FirstElement = 0;
		return desc;
	}

	static BufferSRVDesc CreateTyped(DXGI_FORMAT format, Buffer* pCounter = nullptr)
	{
		BufferSRVDesc desc;
		desc.Format = format;
		desc.FirstElement = 0;
		return desc;
	}

	static BufferSRVDesc CreateByteAddress()
	{
		BufferSRVDesc desc;
		desc.Format = DXGI_FORMAT_R32_TYPELESS;
		desc.FirstElement = 0;
		return desc;
	}

	DXGI_FORMAT Format;
	int FirstElement;
};

class Buffer : public GraphicsResource
{
public:
	Buffer() = default;
	Buffer(ID3D12Resource* pResource, D3D12_RESOURCE_STATES state);
	void Create(Graphics* pGraphics, const BufferDesc& desc);
	void SetData(CommandContext* pContext, const void* pData, uint64 dataSize, uint32 offset = 0);

	void* Map(uint32 subResource = 0, uint64 readFrom = 0, uint64 readTo = 0);
	void Unmap(uint32 subResource = 0, uint64 writtenFrom = 0, uint64 writtenTo = 0);

	inline uint64 GetSize() const { return m_Desc.ElementCount * m_Desc.ElementSize; }
	const BufferDesc& GetDesc() const { return m_Desc; }
private:
	BufferDesc m_Desc;
};

class DescriptorBase
{
public:
	Buffer* GetParent() const { return m_pParent; }
	D3D12_CPU_DESCRIPTOR_HANDLE GetDescriptor() const { return m_Descriptor; }
protected:
	Buffer* m_pParent = nullptr;
	CD3DX12_CPU_DESCRIPTOR_HANDLE m_Descriptor = {};
};

class BufferSRV : public DescriptorBase
{
public:
	BufferSRV() = default;
	void Create(Graphics* pGraphics, Buffer* pBuffer, const BufferSRVDesc& desc);
};

class BufferUAV : public DescriptorBase
{
public:
	BufferUAV() = default;
	void Create(Graphics* pGraphics, Buffer* pBuffer, const BufferUAVDesc& desc);
};