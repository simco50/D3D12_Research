#pragma once
#include "GraphicsResource.h"
#include "DescriptorHandle.h"

class Buffer;
class Texture;
class GraphicsResource;

struct BufferUAVDesc
{
	BufferUAVDesc(DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN, bool raw = false, bool counter = false)
		: Format(format), Raw(raw), Counter(counter)
	{}

	static BufferUAVDesc CreateRaw()
	{
		return BufferUAVDesc(DXGI_FORMAT_UNKNOWN, true, false);
	}

	DXGI_FORMAT Format;
	bool Raw;
	bool Counter;
};

struct BufferSRVDesc
{
	BufferSRVDesc(DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN, bool raw = false, uint32 elementOffset = 0, uint32 numElements = 0)
		: Format(format), Raw(raw), ElementOffset(elementOffset), NumElements(numElements)
	{}

	DXGI_FORMAT Format;
	bool Raw;
	uint32 ElementOffset;
	uint32 NumElements;
};

struct TextureSRVDesc
{
	TextureSRVDesc(uint8 mipLevel = 0)
		: MipLevel(mipLevel)
	{}

	uint8 MipLevel;

	bool operator==(const TextureSRVDesc& other) const
	{
		return MipLevel == other.MipLevel;
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

class ResourceView : public GraphicsObject
{
public:
	ResourceView(GraphicsResource* pParent, D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptor, DescriptorHandle gpuDescriptor);
	virtual ~ResourceView();
	GraphicsResource* GetResource() const { return m_pResource; }
	D3D12_CPU_DESCRIPTOR_HANDLE GetDescriptor() const { return m_Descriptor; }
	uint32 GetHeapIndex() const { return m_GpuDescriptor.HeapIndex; }
	uint64 GetGPUView() const { return m_GpuDescriptor.GpuHandle.ptr; }
protected:
	GraphicsResource* m_pResource = nullptr;
	D3D12_CPU_DESCRIPTOR_HANDLE m_Descriptor = {};
	DescriptorHandle m_GpuDescriptor;
};

class ShaderResourceView : public ResourceView
{
public:
	ShaderResourceView(GraphicsResource* pParent, D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptor, DescriptorHandle gpuDescriptor);
};

class UnorderedAccessView : public ResourceView
{
public:
	UnorderedAccessView(GraphicsResource* pParent, D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptor, DescriptorHandle gpuDescriptor, Buffer* pCounter = nullptr);

	Buffer* GetCounter() const { return m_pCounter; }
	UnorderedAccessView* GetCounterUAV() const;
	ShaderResourceView* GetCounterSRV() const;

private:
	RefCountPtr<Buffer> m_pCounter;
};
