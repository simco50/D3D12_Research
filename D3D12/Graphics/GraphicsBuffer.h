#pragma once
#include "GraphicsResource.h"
class CommandContext;
class Graphics;
class Buffer;
class ShaderResourceView;
class UnorderedAccessView;
class DescriptorBase;
struct BufferSRVDesc;
struct BufferUAVDesc;

enum class BufferFlag
{
	None = 0,
	UnorderedAccess = 1 << 0,
	ShaderResource = 1 << 1,
	Upload = 1 << 2,
	Readback = 1 << 3,
	Structured = 1 << 4,
	ByteAddress = 1 << 5,
	IndirectArguments = 1 << 6,
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

	static BufferDesc CreateByteAddress(int bytes, BufferFlag usage = BufferFlag::ShaderResource)
	{
		assert(bytes % 4 == 0);
		BufferDesc desc;
		desc.ElementCount = bytes / 4;
		desc.ElementSize = 4;
		desc.Usage = usage | BufferFlag::ByteAddress | BufferFlag::UnorderedAccess;
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
	static BufferDesc CreateIndirectArguments(int elements = 1, BufferFlag usage = BufferFlag::None)
	{
		BufferDesc desc;
		desc.ElementCount = elements;
		desc.ElementSize = sizeof(IndirectParameters);
		desc.Usage = usage | BufferFlag::IndirectArguments | BufferFlag::UnorderedAccess;
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

class Buffer : public GraphicsResource
{
public:
	Buffer(Graphics* pGraphics, const char* pName = "");
	Buffer(Graphics* pGraphics, ID3D12Resource * pResource, D3D12_RESOURCE_STATES state);
	~Buffer();
	void Create(const BufferDesc& desc);
	void SetData(CommandContext* pContext, const void* pData, uint64 dataSize, uint32 offset = 0);

	void* Map(uint32 subResource = 0, uint64 readFrom = 0, uint64 readTo = 0);
	void Unmap(uint32 subResource = 0, uint64 writtenFrom = 0, uint64 writtenTo = 0);

	inline uint64 GetSize() const { return (uint64)m_Desc.ElementCount * m_Desc.ElementSize; }
	const BufferDesc& GetDesc() const { return m_Desc; }

	void CreateUAV(UnorderedAccessView** pView, const BufferUAVDesc& desc);
	void CreateSRV(ShaderResourceView** pView, const BufferSRVDesc& desc);

	D3D12_CPU_DESCRIPTOR_HANDLE GetSRV() const;
	D3D12_CPU_DESCRIPTOR_HANDLE GetUAV() const;
	Buffer* GetCounter() const { return m_pCounter.get(); }

protected:
	UnorderedAccessView* m_pUav = nullptr;
	ShaderResourceView* m_pSrv = nullptr;
	std::unique_ptr<Buffer> m_pCounter;

	std::vector<std::unique_ptr<DescriptorBase>> m_Descriptors;
	BufferDesc m_Desc;
	const char* m_pName = nullptr;
};