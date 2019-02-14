#pragma once

class CommandAllocatorPool;
class CommandQueue;

struct CommandContext
{
	std::unique_ptr<ID3D12GraphicsCommandList> pCommandList;
	ID3D12CommandAllocator* pAllocator;
};

class CommandQueueManager
{
public:
	CommandQueueManager(ID3D12Device* pDevice);

	CommandQueue* GetMainCommandQueue() const;

	CommandContext* AllocatorCommandList(D3D12_COMMAND_LIST_TYPE type);
	void FreeCommandList(CommandContext* pCommandList);

private:
	ID3D12Device* m_pDevice;

	std::map<D3D12_COMMAND_LIST_TYPE, std::unique_ptr<CommandQueue>> m_CommandQueues;
	std::vector<CommandContext> m_CommandListPool;
	std::queue<CommandContext*> m_FreeCommandLists;
};

enum class CommandQueueType
{
	Graphics,
	Compute,
	Copy,
	MAX,
};

class CommandQueue
{
public:
	CommandQueue(ID3D12Device* pDevice, CommandQueueType type);
	~CommandQueue();

	uint64 ExecuteCommandList(CommandContext* pCommandContext, bool waitForCompletion = false);
	ID3D12CommandQueue* GetCommandQueue() const { return m_pCommandQueue.Get(); }

	bool IsFenceComplete(uint64 fenceValue);
	void InsertWait(uint64 fenceValue);
	void InsertWaitForQueueFence(CommandQueue* pQueue, uint64 fenceValue);
	void InsertWaitForQueue(CommandQueue* pQueue);

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