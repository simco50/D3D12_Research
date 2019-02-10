#pragma once

class GpuResource
{
public:
	GpuResource()
	{}

	GpuResource(D3D12_RESOURCE_STATES resourceState)
	{}

	D3D12_RESOURCE_STATES GetResourceState() const { return m_CurrentResourceState; }
	void SetResourceState(const D3D12_RESOURCE_STATES resourceState) { m_CurrentResourceState = resourceState; }

	ID3D12Resource* GetResource() const { return m_pResource.Get(); }

protected:
	D3D12_RESOURCE_STATES m_CurrentResourceState;
	ComPtr<ID3D12Resource> m_pResource;

private:

};