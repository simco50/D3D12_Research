#pragma once

class Graphics;
class ResourceView;

class GraphicsObject
{
public:
	GraphicsObject(Graphics* pParent = nullptr)
		: m_pParent(pParent)
	{}

	Graphics* GetParent() const { return m_pParent; }

protected:
	Graphics* m_pParent;
};

constexpr D3D12_RESOURCE_STATES D3D12_RESOURCE_STATE_UNKNOWN = (D3D12_RESOURCE_STATES)-1;

class ResourceState
{
public:
	ResourceState(D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_UNKNOWN)
		: m_CommonState(initialState), m_AllSameState(true)
	{}
	void Set(D3D12_RESOURCE_STATES state, int subResource)
	{
		if (subResource != D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
		{
			check(subResource < m_ResourceStates.size());
			if (m_AllSameState)
			{
				for (D3D12_RESOURCE_STATES& s : m_ResourceStates)
				{
					s = m_CommonState;
				}
				m_AllSameState = false;
			}
			m_ResourceStates[subResource] = state;
		}
		else
		{
			m_AllSameState = true;
			m_CommonState = state;
		}
	}
	D3D12_RESOURCE_STATES Get(int subResource) const
	{
		assert(m_AllSameState || subResource < m_ResourceStates.size());
		return m_AllSameState ? m_CommonState : m_ResourceStates[subResource];
	}
private:
	constexpr static uint32 MAX_SUBRESOURCES = 12;
	std::array<D3D12_RESOURCE_STATES, MAX_SUBRESOURCES> m_ResourceStates{};
	D3D12_RESOURCE_STATES m_CommonState;
	bool m_AllSameState;
};

class GraphicsResource : public GraphicsObject
{
	friend class CommandContext;

public:
	GraphicsResource(Graphics* pParent);
	GraphicsResource(Graphics* pParent, ID3D12Resource* pResource, D3D12_RESOURCE_STATES state);
	virtual ~GraphicsResource();

	void Release();
	void SetName(const char* pName);
	std::string GetName() const;

public:
	inline ID3D12Resource* GetResource() const { return m_pResource; }
	inline D3D12_GPU_VIRTUAL_ADDRESS GetGpuHandle() const { return m_pResource->GetGPUVirtualAddress(); }

	void SetResourceState(D3D12_RESOURCE_STATES state, uint32 subResource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) { m_ResourceState.Set(state, subResource); }
	inline D3D12_RESOURCE_STATES GetResourceState(uint32 subResource = 0) const { return m_ResourceState.Get(subResource); }

protected:
	ID3D12Resource* m_pResource = nullptr;
	std::vector<std::unique_ptr<ResourceView>> m_Descriptors;
	ResourceState m_ResourceState;
};