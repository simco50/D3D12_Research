#pragma once
#include "GraphicsResource.h"

class CommandContext;
class CommandQueue;

class Fence : public GraphicsObject
{
public:
	Fence(GraphicsDevice* pParent, uint64 fenceValue, const char* pName);
	~Fence();

	// Signals on the GPU timeline, increments the next value and return the signaled fence value
	uint64 Signal(CommandQueue* pQueue);
	// Inserts a wait on the GPU timeline
	void GpuWait(CommandQueue* pQueue, uint64 fenceValue);
	// Stall CPU until fence value is signaled on the GPU
	void CpuWait(uint64 fenceValue);
	// Returns true if the fence has reached this value or higher
	bool IsComplete(uint64 fenceValue);
	// Get the fence value that will get signaled next
	uint64 GetCurrentValue() const { return m_CurrentValue; }
	uint64 GetLastSignaledValue() const { return m_LastSignaled; }

	inline ID3D12Fence* GetFence() const { return m_pFence.Get(); }


private:
	ComPtr<ID3D12Fence> m_pFence;
	std::mutex m_FenceWaitCS;
	HANDLE m_CompleteEvent;
	uint64 m_CurrentValue;
	uint64 m_LastSignaled;
	uint64 m_LastCompleted;
};

class CommandQueue : public GraphicsObject
{
public:
	CommandQueue(GraphicsDevice* pParent, D3D12_COMMAND_LIST_TYPE type);

	uint64 ExecuteCommandLists(CommandContext** pCommandContexts, uint32 numContexts);
	ID3D12CommandQueue* GetCommandQueue() const { return m_pCommandQueue.Get(); }

	void InsertWait(uint64 fenceValue);

	//Block on the CPU side
	void WaitForFence(uint64 fenceValue);
	void WaitForIdle();

	Fence* GetFence() const { return m_pFence.get(); }
	D3D12_COMMAND_LIST_TYPE GetType() const { return m_Type; }

	ID3D12CommandAllocator* RequestAllocator();
	void FreeAllocator(uint64 fenceValue, ID3D12CommandAllocator* pAllocator);

private:
	ComPtr<ID3D12GraphicsCommandList> m_pTransitionCommandlist;

	std::vector<ComPtr<ID3D12CommandAllocator>> m_CommandAllocators;
	std::queue<std::pair<ID3D12CommandAllocator*, uint64>> m_FreeAllocators;
	std::mutex m_AllocationMutex;

    ComPtr<ID3D12CommandQueue> m_pCommandQueue;
    std::mutex m_FenceMutex;
	std::unique_ptr<Fence> m_pFence;

	D3D12_COMMAND_LIST_TYPE m_Type;
};
