#include "stdafx.h"
#include "CommandQueue.h"
#include "Graphics.h"
#include "CommandContext.h"
#include "D3D.h"
#include "Core/Profiler.h"

CommandQueue::CommandQueue(GraphicsDevice* pParent, D3D12_COMMAND_LIST_TYPE type)
	: DeviceObject(pParent),
	m_Type(type)
{
	D3D12_COMMAND_QUEUE_DESC desc = {};
	desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	desc.NodeMask = 0;
	desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	desc.Type = type;

	m_pFence = new Fence(pParent, "CommandQueue Fence");

	VERIFY_HR_EX(pParent->GetDevice()->CreateCommandQueue(&desc, IID_PPV_ARGS(m_pCommandQueue.GetAddressOf())), pParent->GetDevice());
	D3D::SetObjectName(m_pCommandQueue.Get(), Sprintf("%s CommandQueue", D3D::CommandlistTypeToString(type)).c_str());

	VERIFY_HR(m_pCommandQueue->GetTimestampFrequency(&m_TimestampFrequency));
}

SyncPoint CommandQueue::ExecuteCommandLists(Span<CommandContext* const> contexts)
{
	PROFILE_CPU_SCOPE();
	check(contexts.GetSize());

	// Commandlists can be recorded in parallel.
	// The before state of a resource transition can't be known so commandlists keep local resource states
	// and insert "pending resource barriers" which are barriers with an unknown before state.
	// During commandlist execution, these pending resource barriers are resolved by inserting
	// new barriers in the previous commandlist before closing it.
	// The first commandlist will resolve the barriers of the next so the first one will just contain resource barriers.

	std::vector<ID3D12CommandList*> commandLists;
	commandLists.reserve(contexts.GetSize() + 1);

	CommandContext* pBarrierCommandlist = GetParent()->AllocateCommandContext(m_Type);
	CommandContext* pCurrentContext = pBarrierCommandlist;

	// Executing a commandlist will update the last sync point and resource state tracking.
	// Can't have multiple threads do this at the same time.
	std::lock_guard lock(m_ExecuteLock);

	for(CommandContext* pNextContext : contexts)
	{
		check(pNextContext);

		pNextContext->ResolvePendingBarriers(*pCurrentContext);

		VERIFY_HR_EX(pCurrentContext->GetCommandList()->Close(), GetParent()->GetDevice());
		commandLists.push_back(pCurrentContext->GetCommandList());

		pCurrentContext = pNextContext;
	}
	VERIFY_HR_EX(pCurrentContext->GetCommandList()->Close(), GetParent()->GetDevice());
	commandLists.push_back(pCurrentContext->GetCommandList());

	{
		PROFILE_CPU_SCOPE("ExecuteCommandLists");
		PROFILE_EXECUTE_COMMANDLISTS(m_pCommandQueue, commandLists);
		m_pCommandQueue->ExecuteCommandLists((uint32)commandLists.size(), commandLists.data());
	}

	uint64 fenceValue = m_pFence->Signal(this);
	m_SyncPoint = SyncPoint(m_pFence, fenceValue);

	pBarrierCommandlist->Free(m_SyncPoint);

	return m_SyncPoint;
}

Ref<ID3D12CommandAllocator> CommandQueue::RequestAllocator()
{
	auto CreateAllocator = [this]() {
		Ref<ID3D12CommandAllocator> pAllocator;
		GetParent()->GetDevice()->CreateCommandAllocator(m_Type, IID_PPV_ARGS(pAllocator.GetAddressOf()));
		D3D::SetObjectName(pAllocator.Get(), Sprintf("Pooled Allocator %d - %s", (int)m_AllocatorPool.GetSize(), D3D::CommandlistTypeToString(m_Type)).c_str());
		return pAllocator;
	};
	Ref<ID3D12CommandAllocator> pAllocator = m_AllocatorPool.Allocate(CreateAllocator);
	pAllocator->Reset();
	return pAllocator;
}

void CommandQueue::FreeAllocator(const SyncPoint& syncPoint, Ref<ID3D12CommandAllocator>& pAllocator)
{
	m_AllocatorPool.Free(std::move(pAllocator), syncPoint);
}

void CommandQueue::InsertWait(const SyncPoint& syncPoint)
{
	if(syncPoint.IsValid())
		m_pCommandQueue->Wait(syncPoint.GetFence()->GetFence(), syncPoint.GetFenceValue());
}

void CommandQueue::InsertWait(CommandQueue* pQueue)
{
	InsertWait(pQueue->m_SyncPoint);
}

void CommandQueue::WaitForIdle()
{
	uint64 fenceValue = m_pFence->Signal(this);
	m_pFence->CpuWait(fenceValue);
}

void SyncPoint::Wait() const
{
	m_pFence->CpuWait(m_FenceValue);
}

bool SyncPoint::IsComplete() const
{
	return m_pFence->IsComplete(m_FenceValue);
}
