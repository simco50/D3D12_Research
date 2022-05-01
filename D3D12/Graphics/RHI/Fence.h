#pragma once
#include "GraphicsResource.h"

class CommandQueue;

class Fence : public GraphicsObject
{
public:
	Fence(GraphicsDevice* pParent, const char* pName, uint64 fenceValue = 0);
	~Fence();

	// Signals on the GPU timeline, increments the next value and return the signaled fence value
	uint64 Signal(CommandQueue* pQueue);
	uint64 Signal(uint64 fenceValue);
	// Stall CPU until fence value is signaled on the GPU
	void CpuWait(uint64 fenceValue);
	// Returns true if the fence has reached this value or higher
	bool IsComplete(uint64 fenceValue);
	// Get the fence value that will get signaled next
	uint64 GetCurrentValue() const { return m_CurrentValue; }
	uint64 GetLastSignaledValue() const { return m_LastSignaled; }

	inline ID3D12Fence* GetFence() const { return m_pFence.Get(); }

private:
	RefCountPtr<ID3D12Fence> m_pFence;
	std::mutex m_FenceWaitCS;
	HANDLE m_CompleteEvent;
	uint64 m_CurrentValue;
	uint64 m_LastSignaled;
	uint64 m_LastCompleted;
};

class SyncPoint
{
public:
	SyncPoint() = default;
	SyncPoint(Fence* pFence, uint64 fenceValue)
		: m_pFence(pFence), m_FenceValue(fenceValue)
	{}

	void Wait() const;
	bool IsComplete() const;
	uint64 GetFenceValue() const { return m_FenceValue; }
	Fence* GetFence() const { return m_pFence; }
	bool IsValid() const { return !!m_pFence; }
	operator bool() const { return IsValid(); }

private:
	Fence* m_pFence = nullptr;
	uint64 m_FenceValue = 0;
};

template<typename ObjectType, bool ThreadSafe>
class FencedPool
{
public:
	FencedPool() = default;
	FencedPool(const FencedPool& rhs) = delete;
	FencedPool* operator=(const FencedPool& rhs) = delete;

	struct DummyMutex
	{
		void lock() {}
		void unlock() {}
	};
	using TMutex = std::conditional_t<ThreadSafe, std::mutex, DummyMutex>;

	template<typename CreateFn>
	ObjectType Allocate(CreateFn&& createFn)
	{
		std::lock_guard lock(m_Mutex);
		if (m_ObjectPool.empty() || !m_ObjectPool.front().second.IsComplete())
		{
			return createFn();
		}
		ObjectType object = std::move(m_ObjectPool.front().first);
		m_ObjectPool.pop();
		return object;
	}

	void Free(ObjectType&& object, const SyncPoint& syncPoint)
	{
		std::lock_guard lock(m_Mutex);
		m_ObjectPool.push({ std::move(object), syncPoint });
	}

	uint32 GetSize() const { return (uint32)m_ObjectPool.size(); }

private:
	std::queue<std::pair<ObjectType, SyncPoint>> m_ObjectPool;
	TMutex m_Mutex;
};
