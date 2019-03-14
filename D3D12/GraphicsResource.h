#pragma once
class CommandContext;
class Graphics;

class GraphicsResource
{
	friend class CommandContext;

public:
	GraphicsResource() {}
	GraphicsResource(ID3D12Resource* pResource, D3D12_RESOURCE_STATES state) : m_pResource(pResource), m_CurrentState(state) {}
	virtual ~GraphicsResource()
	{
		if (m_pResource)
		{
			m_pResource->Release();
		}
	}

public:
	ID3D12Resource* GetResource() const { return m_pResource; }
	ID3D12Resource** GetResourceAddressOf() { return &m_pResource; }
	D3D12_GPU_VIRTUAL_ADDRESS GetGpuHandle() const { return m_pResource->GetGPUVirtualAddress(); }

	D3D12_RESOURCE_STATES GetResourceState() const { return m_CurrentState; }

protected:
	ID3D12Resource* m_pResource;
	D3D12_RESOURCE_STATES m_CurrentState;
};

class GraphicsBuffer : public GraphicsResource
{
public:
	void Create(ID3D12Device* pDevice, uint32 size, bool cpuVisible = false);
	void SetData(CommandContext* pContext, void* pData, uint32 dataSize);

	uint32 GetSize() const { return m_Size; }

private:
	uint32 m_Size;
};

enum class TextureUsage
{
	/*Default				= 0,*/
	/*Dynamic				= 1 << 0,*/
	UnorderedAccess		= 1 << 1,
	ShaderResource		= 1 << 2,
	RenderTarget		= 1 << 3,
	DepthStencil		= 1 << 4,
};
DEFINE_ENUM_FLAG_OPERATORS(TextureUsage)

class Texture2D : public GraphicsResource
{
public:
	void Create(Graphics* pGraphics, CommandContext* pContext, const char* pFilePath, TextureUsage usage);
	void Create(Graphics* pGraphics, int width, int height, DXGI_FORMAT format, TextureUsage usage);
	void SetData(CommandContext* pContext, const void* pData, uint32 dataSize);
	void CreateForSwapchain(Graphics* pGraphics, ID3D12Resource* pTexture);

	D3D12_CPU_DESCRIPTOR_HANDLE GetRTV() const { return m_Rtv; }
	D3D12_CPU_DESCRIPTOR_HANDLE GetSRV() const { return m_Srv; }
	D3D12_CPU_DESCRIPTOR_HANDLE GetUAV() const { return m_Uav; }

	int GetWidth() const { return m_Width; }
	int GetHeight() const { return m_Height; }
	int GetMipLevels() const { return m_MipLevels; }

	int GetRowDataSize(unsigned int width) const;
private:
	static DXGI_FORMAT GetDepthFormat(DXGI_FORMAT format);

	int m_Width;
	int m_Height;
	DXGI_FORMAT m_Format;
	int m_MipLevels = 1;
	D3D12_CPU_DESCRIPTOR_HANDLE m_Rtv;
	D3D12_CPU_DESCRIPTOR_HANDLE m_Srv;
	D3D12_CPU_DESCRIPTOR_HANDLE m_Uav;
};