#pragma once
#include "GraphicsResource.h"
class CommandContext;
class Graphics;
class Buffer;
class ShaderResourceView;
class UnorderedAccessView;
class ResourceView;
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
	AccelerationStructure = 1 << 7,
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
		return BufferDesc(size, sizeof(uint8), BufferFlag::Readback);
	}

	static BufferDesc CreateByteAddress(uint64 bytes, BufferFlag usage = BufferFlag::ShaderResource)
	{
		check(bytes % 4 == 0);
		BufferDesc desc;
		desc.ElementCount = (uint32)(bytes / 4);
		desc.ElementSize = 4;
		desc.Usage = usage | BufferFlag::ByteAddress | BufferFlag::UnorderedAccess;
		return desc;
	}

	static BufferDesc CreateAccelerationStructure(uint64 bytes)
	{
		check(bytes % 4 == 0);
		BufferDesc desc;
		desc.ElementCount = (uint32)(bytes / 4);
		desc.ElementSize = 4;
		desc.Usage = desc.Usage | BufferFlag::AccelerationStructure | BufferFlag::UnorderedAccess;
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

	static BufferDesc CreateTyped(int elementCount, DXGI_FORMAT format, BufferFlag usage = BufferFlag::ShaderResource | BufferFlag::UnorderedAccess)
	{
		check(!D3D::IsBlockCompressFormat(format));
		BufferDesc desc;
		desc.ElementCount = elementCount;
		desc.ElementSize = D3D::GetFormatRowDataSize(format, 1);
		desc.Format = format;
		desc.Usage = usage;
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
	DXGI_FORMAT Format = DXGI_FORMAT_UNKNOWN;
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

	//#todo: Temp code. Pull out views from buffer
	ShaderResourceView* GetSRV() const { return m_pSrv; };
	UnorderedAccessView* GetUAV() const { return m_pUav; };

protected:
	//#todo: Temp code. Pull out views from buffer
	UnorderedAccessView* m_pUav = nullptr;
	ShaderResourceView* m_pSrv = nullptr;

	BufferDesc m_Desc;
	std::string m_Name;
};