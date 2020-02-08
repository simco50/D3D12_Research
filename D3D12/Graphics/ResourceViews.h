#pragma once
#include "GraphicsResource.h"

class Buffer;
class Graphics;
class Texture;
class GraphicsResource;

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

struct TextureSRVDesc
{
	DXGI_FORMAT Format;
	uint8 MipLevel;
	uint8 NumMipLevels;
	uint32 FirstArraySlice;
	uint32 NumArraySlices;

	bool operator==(const TextureSRVDesc& other) const
	{
		return Format == other.Format &&
			MipLevel == other.MipLevel &&
			NumMipLevels == other.NumMipLevels &&
			FirstArraySlice == other.FirstArraySlice &&
			NumArraySlices == other.NumArraySlices;
	}
};

struct TextureUAVDesc
{
	uint8 MipLevel;

	bool operator==(const TextureUAVDesc& other) const
	{
		return MipLevel == other.MipLevel;
	}
};

class DescriptorBase : public GraphicsObject
{
public:
	DescriptorBase(Graphics* pGraphics);
	GraphicsResource* GetParent() const { return m_pParent; }
	D3D12_CPU_DESCRIPTOR_HANDLE GetDescriptor() const { return m_Descriptor; }
protected:
	GraphicsResource* m_pParent = nullptr;
	CD3DX12_CPU_DESCRIPTOR_HANDLE m_Descriptor = {};
};

class ShaderResourceView : public DescriptorBase
{
public:
	ShaderResourceView(Graphics* pGraphics);
	~ShaderResourceView();
	void Create(Buffer* pBuffer, const BufferSRVDesc& desc);
	void Create(Texture* pTexture, const TextureSRVDesc& desc);
	void Release();
};

class UnorderedAccessView : public DescriptorBase
{
public:
	UnorderedAccessView(Graphics* pGraphics);
	~UnorderedAccessView();
	void Create(Buffer* pBuffer, const BufferUAVDesc& desc);
	void Create(Texture* pTexture, const TextureUAVDesc& desc);
	void Release();
};