#pragma once
#include "GraphicsResource.h"

class Buffer;
class Graphics;
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
	BufferSRVDesc(DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN, bool raw = false)
		: Format(format), Raw(raw)
	{}

	DXGI_FORMAT Format;
	bool Raw;
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

class ResourceView
{
public:
	virtual ~ResourceView() = default;
	GraphicsResource* GetParent() const { return m_pParent; }
	D3D12_CPU_DESCRIPTOR_HANDLE GetDescriptor() const { return m_Descriptor; }
protected:
	GraphicsResource* m_pParent = nullptr;
	CD3DX12_CPU_DESCRIPTOR_HANDLE m_Descriptor = {};
};

class ShaderResourceView : public ResourceView
{
public:
	~ShaderResourceView();
	void Create(Buffer* pBuffer, const BufferSRVDesc& desc);
	void Create(Texture* pTexture, const TextureSRVDesc& desc);
	void Release();
};

class UnorderedAccessView : public ResourceView
{
public:
	~UnorderedAccessView();
	void Create(Buffer* pBuffer, const BufferUAVDesc& desc);
	void Create(Texture* pTexture, const TextureUAVDesc& desc);
	void Release();

	Buffer* GetCounter() const { return m_pCounter.get(); }
	UnorderedAccessView* GetCounterUAV() const;
	ShaderResourceView* GetCounterSRV() const;

private:
	std::unique_ptr<Buffer> m_pCounter;
};