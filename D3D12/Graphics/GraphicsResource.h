#pragma once

class GraphicsResource
{
	friend class CommandContext;

public:
	GraphicsResource();
	GraphicsResource(ID3D12Resource* pResource, D3D12_RESOURCE_STATES state);
	virtual ~GraphicsResource();

	void Release();
	void SetName(const char* pName);

public:
	inline ID3D12Resource* GetResource() const { return m_pResource.Get(); }
	inline ID3D12Resource** GetResourceAddressOf() { return m_pResource.GetAddressOf(); }
	inline D3D12_GPU_VIRTUAL_ADDRESS GetGpuHandle() const { return m_pResource->GetGPUVirtualAddress(); }

	inline D3D12_RESOURCE_STATES GetResourceState() const { return m_CurrentState; }

protected:
	ComPtr<ID3D12Resource> m_pResource;
	D3D12_RESOURCE_STATES m_CurrentState;
};