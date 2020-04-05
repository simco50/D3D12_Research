#pragma once
#include "GraphicsResource.h"

class CommandAllocatorPool;
class Graphics;

class CommandQueue : public GraphicsObject
{
public:
	CommandQueue(Graphics* pGraphics, D3D12_COMMAND_LIST_TYPE type);
	~CommandQueue();

	uint64 ExecuteCommandList(ID3D12CommandList* pCommandContext);
	ID3D12CommandQueue* GetCommandQueue() const { return m_pCommandQueue.Get(); }

	//Inserts a stall/wait in the queue so it blocks the GPU
	void InsertWaitForFence(uint64 fenceValue);
	void InsertWaitForQueue(CommandQueue* pQueue);

	//Block on the CPU side
	void WaitForFence(uint64 fenceValue);
	void WaitForIdle();

	bool IsFenceComplete(uint64 fenceValue);
	uint64 IncrementFence();

	uint64 GetLastCompletedFence() const { return m_LastCompletedFenceValue; }
	uint64 GetNextFenceValue() const { return m_NextFenceValue; }
	ID3D12Fence* GetFence() const { return m_pFence.Get(); }

	ID3D12CommandAllocator* RequestAllocator();
	void FreeAllocator(uint64 fenceValue, ID3D12CommandAllocator* pAllocator);

private:
	std::unique_ptr<CommandAllocatorPool> m_pAllocatorPool;

    ComPtr<ID3D12CommandQueue> m_pCommandQueue;
    std::mutex m_FenceMutex;
    std::mutex m_EventMutex;

	uint64 m_NextFenceValue = 0;
	uint64 m_LastCompletedFenceValue = 0;

    ComPtr<ID3D12Fence> m_pFence;
	HANDLE m_pFenceEventHandle = nullptr;
};