#pragma once
#include "RHI.h"

template<typename T>
class RefCounted
{
public:
	void AddRef()
	{
		m_RefCount.fetch_add(1);
	}

	void Release()
	{
		uint32 count_prev = m_RefCount.fetch_sub(1);
		check(count_prev >= 1);
		if (count_prev == 1)
			Destroy();
	}

	uint32 GetNumRefs() const { return m_RefCount; }

private:
	void Destroy() { delete static_cast<T*>(this); }

	std::atomic<uint32> m_RefCount = 0;
};

class GraphicsObject : public RefCounted<GraphicsObject>
{
public:
	GraphicsObject(GraphicsDevice* pParent)
		: m_pParent(pParent)
	{}
	virtual ~GraphicsObject() = default;

	GraphicsDevice* GetParent() const { return m_pParent; }

private:
	GraphicsDevice* m_pParent;
};

constexpr D3D12_RESOURCE_STATES D3D12_RESOURCE_STATE_UNKNOWN = (D3D12_RESOURCE_STATES)-1;

class ResourceState
{
public:
	ResourceState(D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_UNKNOWN)
		: m_CommonState(initialState), m_AllSameState(true)
	{}

	void Set(D3D12_RESOURCE_STATES state, uint32 subResource)
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
	D3D12_RESOURCE_STATES Get(uint32 subResource) const
	{
		check(m_AllSameState || subResource < (uint32)m_ResourceStates.size());
		if (m_AllSameState)
			return m_CommonState;

		return m_ResourceStates[subResource];
	}

	static bool HasWriteResourceState(D3D12_RESOURCE_STATES state)
	{
		return EnumHasAnyFlags(state,
			D3D12_RESOURCE_STATE_STREAM_OUT |
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS |
			D3D12_RESOURCE_STATE_RENDER_TARGET |
			D3D12_RESOURCE_STATE_DEPTH_WRITE |
			D3D12_RESOURCE_STATE_COPY_DEST |
			D3D12_RESOURCE_STATE_RESOLVE_DEST |
			D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE |
			D3D12_RESOURCE_STATE_VIDEO_PROCESS_WRITE |
			D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE
		);
	};

	static bool CanCombineResourceState(D3D12_RESOURCE_STATES stateA, D3D12_RESOURCE_STATES stateB)
	{
		return !HasWriteResourceState(stateA) && !HasWriteResourceState(stateB);
	}

private:
	std::array<D3D12_RESOURCE_STATES, D3D12_REQ_MIP_LEVELS> m_ResourceStates{};
	D3D12_RESOURCE_STATES m_CommonState;
	bool m_AllSameState;
};

class GraphicsResource : public GraphicsObject
{
	friend class GraphicsDevice;

public:
	GraphicsResource(GraphicsDevice* pParent, ID3D12Resource* pResource);
	~GraphicsResource();

	void* GetMappedData() const { check(m_pMappedData); return m_pMappedData; }
	void SetImmediateDelete(bool immediate) { m_ImmediateDelete = immediate; }

	void SetName(const char* pName);
	const char* GetName() const { return m_Name.c_str(); }

	bool UseStateTracking() const { return m_NeedsStateTracking; }

	inline ID3D12Resource* GetResource() const { return m_pResource; }
	inline D3D12_GPU_VIRTUAL_ADDRESS GetGpuHandle() const { return m_pResource->GetGPUVirtualAddress(); }

	void SetResourceState(D3D12_RESOURCE_STATES state, uint32 subResource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) { m_ResourceState.Set(state, subResource); }
	inline D3D12_RESOURCE_STATES GetResourceState(uint32 subResource = 0) const { return m_ResourceState.Get(subResource); }

protected:
	std::string m_Name;
	bool m_ImmediateDelete = false;
	ID3D12Resource* m_pResource = nullptr;
	void* m_pMappedData = nullptr;
	ResourceState m_ResourceState;
	bool m_NeedsStateTracking = false;
};
