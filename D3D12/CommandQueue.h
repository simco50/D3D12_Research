#pragma once

class CommandAllocatorPool;
class CommandContext;

class CommandQueue
{
public:
	CommandQueue(ID3D12Device* pDevice, D3D12_COMMAND_LIST_TYPE type);
	~CommandQueue();

	uint64 ExecuteCommandList(CommandContext* pCommandContext, bool waitForCompletion = false);
	ID3D12CommandQueue* GetCommandQueue() const { return m_pCommandQueue.Get(); }

	bool IsFenceComplete(uint64 fenceValue);
	void InsertWait(uint64 fenceValue);
	void InsertWaitForQueueFence(CommandQueue* pQueue, uint64 fenceValue);
	void InsertWaitForQueue(CommandQueue* pQueue);
	uint64 IncrementFence();

	void WaitForFenceBlock(uint64 fenceValue);
	void WaitForIdle();

	uint64 PollCurrentFenceValue();
	uint64 GetLastCompletedFence() const { return m_LastCompletedFenceValue; }
	uint64 GetNextFenceValue() const { return m_NextFenceValue; }
	ID3D12Fence* GetFence() const { return m_pFence.Get(); }

	CommandAllocatorPool* GetAllocatorPool() const { return m_pAllocatorPool.get(); }

private:
	std::unique_ptr<CommandAllocatorPool> m_pAllocatorPool;

    ComPtr<ID3D12CommandQueue> m_pCommandQueue;
    D3D12_COMMAND_LIST_TYPE m_Type;
    std::mutex m_FenceMutex;
    std::mutex m_EventMutex;

	uint64 m_NextFenceValue = 0;
	uint64 m_LastCompletedFenceValue = 0;

    ComPtr<ID3D12Fence> m_pFence;
	HANDLE m_pFenceEventHandle = nullptr;
};