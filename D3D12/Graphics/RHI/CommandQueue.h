#pragma once
#include "GraphicsResource.h"
#include "Fence.h"

class CommandQueue : public GraphicsObject
{
public:
	CommandQueue(GraphicsDevice* pParent, D3D12_COMMAND_LIST_TYPE type);

	SyncPoint ExecuteCommandLists(const Span<CommandContext* const>& contexts);
	ID3D12CommandQueue* GetCommandQueue() const { return m_pCommandQueue.Get(); }

	void InsertWait(const SyncPoint& syncPoint);
	void InsertWait(CommandQueue* pQueue);

	//Block on the CPU side
	void WaitForFence(uint64 fenceValue);
	void WaitForIdle();

	Fence* GetFence() const { return m_pFence; }
	D3D12_COMMAND_LIST_TYPE GetType() const { return m_Type; }
	uint64 GetTimestampFrequency() const { return m_TimestampFrequency; }

	RefCountPtr<ID3D12CommandAllocator> RequestAllocator();
	void FreeAllocator(const SyncPoint& syncPoint, RefCountPtr<ID3D12CommandAllocator>& pAllocator);

private:
	RefCountPtr<ID3D12CommandQueue> m_pCommandQueue;
	FencedPool<RefCountPtr<ID3D12CommandAllocator>, true> m_AllocatorPool;
	RefCountPtr<ID3D12GraphicsCommandList> m_pTransitionCommandlist;
	RefCountPtr<Fence> m_pFence;
	SyncPoint m_SyncPoint;
	D3D12_COMMAND_LIST_TYPE m_Type;
	uint64 m_TimestampFrequency;
	std::mutex m_ExecuteLock;
};
