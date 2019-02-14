#pragma once

class CommandAllocatorPool;
class CommandContext;

class CommandQueue
{
public:
	CommandQueue(ID3D12Device* pDevice, D3D12_COMMAND_LIST_TYPE type);
	~CommandQueue();

	unsigned long long ExecuteCommandList(CommandContext* pCommandContext, bool waitForCompletion = false);
	ID3D12CommandQueue* GetCommandQueue() const { return m_pCommandQueue.Get(); }

	bool IsFenceComplete(unsigned long long fenceValue);
	void InsertWait(unsigned long long fenceValue);
	void InsertWaitForQueueFence(CommandQueue* pQueue, unsigned long long fenceValue);
	void InsertWaitForQueue(CommandQueue* pQueue);
	unsigned long long IncrementFence();

	void WaitForFenceBlock(unsigned long long fenceValue);
	void WaitForIdle();

	unsigned long long PollCurrentFenceValue();
	unsigned long long GetLastCompletedFence() const { return m_LastCompletedFenceValue; }
	unsigned long long GetNextFenceValue() const { return m_NextFenceValue; }
	ID3D12Fence* GetFence() const { return m_pFence.Get(); }

	CommandAllocatorPool* GetAllocatorPool() const { return m_pAllocatorPool.get(); }

private:
	std::unique_ptr<CommandAllocatorPool> m_pAllocatorPool;

    ComPtr<ID3D12CommandQueue> m_pCommandQueue;
    D3D12_COMMAND_LIST_TYPE m_Type;
    std::mutex m_FenceMutex;
    std::mutex m_EventMutex;

	unsigned long long m_NextFenceValue = 0;
	unsigned long long m_LastCompletedFenceValue = 0;

    ComPtr<ID3D12Fence> m_pFence;
	HANDLE m_pFenceEventHandle = nullptr;
};