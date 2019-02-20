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
	void Create(ID3D12Device* pDevice, int size, bool cpuVisible = false);
	void SetData(CommandContext* pContext, void* pData, int dataSize);

	uint32 GetSize() const { return m_Size; }

private:
	uint32 m_Size;
};

class Texture2D : public GraphicsResource
{
public:
	void Create(Graphics* pGraphics, CommandContext* pContext, const char* pFilePath);
	void Create(Graphics* pGraphics, int width, int height);
	void SetData(CommandContext* pContext, void* pData, int dataSize);

	D3D12_CPU_DESCRIPTOR_HANDLE GetDescriptorHandle() const { return m_DescriptorHandle; }

private:
	int m_Width;
	int m_Height;
	D3D12_CPU_DESCRIPTOR_HANDLE m_DescriptorHandle;
};