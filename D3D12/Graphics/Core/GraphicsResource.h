#pragma once

class Graphics;
class ResourceView;

class GraphicsObject
{
public:
	GraphicsObject(Graphics* pParent = nullptr)
		: m_pGraphics(pParent)
	{}

	Graphics* GetGraphics() const { return m_pGraphics; }

protected:
	Graphics* m_pGraphics;
};

class ResourceState
{
public:
	ResourceState(D3D12_RESOURCE_STATES initialState)
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
	std::array<D3D12_RESOURCE_STATES, 12> m_ResourceStates{};
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