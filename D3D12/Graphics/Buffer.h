#pragma once

#include "GraphicsResource.h"
class CommandContext;
class Graphics;

class Buffer;

enum class BufferUsage
{
	None = 0,
	UnorderedAccess = 1 << 0,
	ShaderResource = 1 << 1,
};
DEFINE_ENUM_FLAG_OPERATORS(BufferUsage)

enum class BufferStorageType
{
	Default,
	Upload,
	Readback,
	MAX
};

struct BufferDesc
{
	BufferDesc() = default;
	explicit BufferDesc(int64 elementCount, int64 stride, BufferUsage usage = BufferUsage::None, bool cpuVisible = false)
		: ElementCount(elementCount), ByteStride(stride), Usage(usage), Storage(cpuVisible ? BufferStorageType::Upload : BufferStorageType::Default)
	{}

	static BufferDesc VertexBuffer(int elements, int elementSize, bool cpuVisible = false)
	{
		BufferDesc desc;
		desc.ElementCount = elements;
		desc.ByteStride = elementSize;
		desc.Usage = BufferUsage::None;
		desc.Storage = cpuVisible ? BufferStorageType::Upload : BufferStorageType::Default;
		return desc;
	}

	static BufferDesc IndexBuffer(int elements, bool cpuVisible = false)
	{
		BufferDesc desc;
		desc.ElementCount = (int64)elements;
		desc.ByteStride = 4;
		desc.Usage = BufferUsage::None;
		desc.Storage = cpuVisible ? BufferStorageType::Upload : BufferStorageType::Default;
		return desc;
	}

	static BufferDesc Readback(int size)
	{
		BufferDesc desc;
		desc.ElementCount = size;
		desc.ByteStride = 8;
		desc.Usage = BufferUsage::None;
		desc.Storage = BufferStorageType::Readback;
		return desc;
	}

	int ElementCount = 0;
	int ByteStride = 0;
	BufferUsage Usage = BufferUsage::None;
	BufferStorageType Storage = BufferStorageType::Default;
};

struct BufferSRVDesc
{
	static BufferSRVDesc Structured(Buffer* pCounter = nullptr)
	{
		BufferSRVDesc desc;
		desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.FirstElement = 0;
		desc.CounterOffset = 0;
		desc.pCounter = pCounter;
		desc.IsRaw = false;
		return desc;
	}

	static BufferSRVDesc ByteAddress()
	{
		BufferSRVDesc desc;
		desc.Format = DXGI_FORMAT_R32_TYPELESS;
		desc.FirstElement = 0;
		desc.CounterOffset = 0;
		desc.pCounter = nullptr;
		desc.IsRaw = true;
		return desc;
	}

	DXGI_FORMAT Format;
	int FirstElement;
	int CounterOffset;
	bool IsRaw;
	Buffer* pCounter;
};

using BufferUAVDesc = BufferSRVDesc;

class Buffer : public GraphicsResource
{
public:
	Buffer() = default;
	Buffer(ID3D12Resource* pResource, D3D12_RESOURCE_STATES state);
	void Create(Graphics* pGraphics, const BufferDesc& desc);
	void SetData(CommandContext* pContext, const void* pData, uint64 dataSize, uint32 offset = 0);

	void* Map(uint32 subResource = 0, uint64 readFrom = 0, uint64 readTo = 0);
	void Unmap(uint32 subResource = 0, uint64 writtenFrom = 0, uint64 writtenTo = 0);

	inline uint64 GetSize() const { return m_Desc.ElementCount * m_Desc.ByteStride; }
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