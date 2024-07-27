#pragma once
#include "RHI.h"

class DeviceObject : public RefCounted<DeviceObject>
{
public:
	DeviceObject(GraphicsDevice* pParent)
		: m_pParent(pParent)
	{}
	virtual ~DeviceObject() = default;

	GraphicsDevice* GetParent() const { return m_pParent; }

private:
	GraphicsDevice* m_pParent;
};

constexpr D3D12_RESOURCE_STATES D3D12_RESOURCE_STATE_UNKNOWN = (D3D12_RESOURCE_STATES)-1;

class ResourceState
{
public:
	ResourceState(D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_UNKNOWN)
		: m_AllSameState(true)
	{
		m_ResourceStates[0] = initialState;
	}

	void Set(D3D12_RESOURCE_STATES state, uint32 subResource)
	{
		if (subResource != D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
		{
			D3D12_RESOURCE_STATES current_state = m_ResourceStates[0];
			gAssert(subResource < m_ResourceStates.size());
			if (m_AllSameState)
			{
				for (D3D12_RESOURCE_STATES& s : m_ResourceStates)
					s = current_state;
				m_AllSameState = false;
			}
			m_ResourceStates[subResource] = state;
		}
		else
		{
			m_AllSameState = true;
			m_ResourceStates[0] = state;
		}
	}
	D3D12_RESOURCE_STATES Get(uint32 subResource) const
	{
		gAssert(m_AllSameState || subResource < (uint32)m_ResourceStates.size());
		if (m_AllSameState)
			return m_ResourceStates[0];
		return m_ResourceStates[subResource];
	}

private:
	StaticArray<D3D12_RESOURCE_STATES, D3D12_REQ_MIP_LEVELS> m_ResourceStates{};
	bool m_AllSameState;
};

class DeviceResource : public DeviceObject
{
public:
	DeviceResource(GraphicsDevice* pParent, ID3D12ResourceX* pResource);
	~DeviceResource();

	void SetImmediateDelete(bool immediate) { m_ImmediateDelete = immediate; }

	void SetName(const char* pName);
	const char* GetName() const { return m_Name.c_str(); }

	bool UseStateTracking() const { return m_NeedsStateTracking; }

	ID3D12ResourceX* GetResource() const { return m_pResource; }
	D3D12_GPU_VIRTUAL_ADDRESS GetGpuHandle() const { return m_pResource->GetGPUVirtualAddress(); }

	void SetResourceState(D3D12_RESOURCE_STATES state, uint32 subResource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) { m_ResourceState.Set(state, subResource); }
	D3D12_RESOURCE_STATES GetResourceState(uint32 subResource = 0) const { return m_ResourceState.Get(subResource); }

protected:
	String m_Name;
	bool m_ImmediateDelete = false;
	ID3D12ResourceX* m_pResource = nullptr;
	ResourceState m_ResourceState;
	bool m_NeedsStateTracking = false;
};
