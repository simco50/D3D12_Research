#include "stdafx.h"
#include "CommandQueue.h"
#include "Graphics.h"
#include "CommandContext.h"
#include "D3DUtils.h"

Fence::Fence(GraphicsDevice* pParent, uint64 fenceValue, const char* pName)
	: GraphicsObject(pParent), m_CurrentValue(fenceValue), m_LastSignaled(0), m_LastCompleted(0)
{
	VERIFY_HR_EX(pParent->GetDevice()->CreateFence(m_LastCompleted, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_pFence.GetAddressOf())), pParent->GetDevice());
	D3D::SetObjectName(m_pFence.Get(), pName);
	m_CompleteEvent = CreateEventExA(nullptr, "Fence Event", 0, EVENT_ALL_ACCESS);
}

Fence::~Fence()
{
	CloseHandle(m_CompleteEvent);
}

uint64 Fence::Signal(CommandQueue* pQueue)
{
	pQueue->GetCommandQueue()->Signal(m_pFence.Get(), m_CurrentValue);
	m_LastSignaled = m_CurrentValue;
	m_CurrentValue++;
	return m_LastSignaled;
}

void Fence::GpuWait(CommandQueue* pQueue, uint64 fenceValue)
{
	VERIFY_HR(pQueue->GetCommandQueue()->Wait(m_pFence.Get(), fenceValue));
}

void Fence::CpuWait(uint64 fenceValue)
{
	if (IsComplete(fenceValue))
	{
		return;
	}

	std::lock_guard<std::mutex> lockGuard(m_FenceWaitCS);

	m_pFence->SetEventOnCompletion(fenceValue, m_CompleteEvent);
	DWORD result = WaitForSingleObject(m_CompleteEvent, INFINITE);

#if USE_PIX
	// The event was successfully signaled, so notify PIX
	if (result == WAIT_OBJECT_0)
	{
		PIXNotifyWakeFromFenceSignal(m_CompleteEvent);
	}
#else
	UNREFERENCED_PARAMETER(result);
#endif

	m_LastCompleted = fenceValue;
}

bool Fence::IsComplete(uint64 fenceValue)
{
	if (fenceValue <= m_LastCompleted)
	{
		return true;
	}
	m_LastCompleted = Math::Max(m_LastCompleted, m_pFence->GetCompletedValue());
	return fenceValue <= m_LastCompleted;
}

CommandQueue::CommandQueue(GraphicsDevice* pParent, D3D12_COMMAND_LIST_TYPE type)
	: GraphicsObject(pParent),
	m_Type(type)
{
	D3D12_COMMAND_QUEUE_DESC desc = {};
	desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	desc.NodeMask = 0;
	desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	desc.Type = type;

	m_pFence = new Fence(pParent, (uint64)type << 56, "CommandQueue Fence");

	VERIFY_HR_EX(pParent->GetDevice()->CreateCommandQueue(&desc, IID_PPV_ARGS(m_pCommandQueue.GetAddressOf())), pParent->GetDevice());
	D3D::SetObjectName(m_pCommandQueue.Get(), Sprintf("%s CommandQueue", D3D::CommandlistTypeToString(type)).c_str());
}

uint64 CommandQueue::ExecuteCommandLists(CommandContext** pCommandContexts, uint32 numContexts)
{
	check(pCommandContexts);
	check(numContexts > 0);

	// Commandlists can be recorded in parallel.
	// The before state of a resource transition can't be known so commandlists keep local resource states
	// and insert "pending resource barriers" which are barriers with an unknown before state.
	// During commandlist execution, these pending resource barriers are resolved by inserting
	// new barriers in the previous commandlist before closing it.
	// The first commandlist will resolve the barriers of the next so the first one will just contain resource barriers.

	std::vector<ID3D12CommandList*> commandLists;
	commandLists.reserve(numContexts + 1);

	CommandContext* pBarrierCommandlist = nullptr;
	CommandContext* pCurrentContext = nullptr;
	for (uint32 i = 0; i < numContexts; ++i)
	{
		CommandContext* pNextContext = pCommandContexts[i];
		check(pNextContext);

		ResourceBarrierBatcher barriers;
		for (const CommandContext::PendingBarrier& pending : pNextContext->GetPendingBarriers())
		{
			uint32 subResource = pending.Subresource;
			GraphicsResource* pResource = pending.pResource;
			D3D12_RESOURCE_STATES beforeState = pResource->GetResourceState(subResource);
			checkf(CommandContext::IsTransitionAllowed(m_Type, beforeState), 
				"Resource (%s) can not be transitioned from this state (%s) on this queue (%s). Insert a barrier on another queue before executing this one.", 
				pResource->GetName().c_str(), D3D::ResourceStateToString(beforeState).c_str(), D3D::CommandlistTypeToString(m_Type));
			barriers.AddTransition(pResource->GetResource(), beforeState, pending.State.Get(subResource), subResource);
			pResource->SetResourceState(pNextContext->GetResourceState(pending.pResource, subResource));
		}
		if (barriers.HasWork())
		{
			if (!pCurrentContext)
			{
				pBarrierCommandlist = GetParent()->AllocateCommandContext(m_Type);
				pCurrentContext = pBarrierCommandlist;
			}
			barriers.Flush(pCurrentContext->GetCommandList());
		}
		if (pCurrentContext)
		{
			VERIFY_HR_EX(pCurrentContext->GetCommandList()->Close(), GetParent()->GetDevice());
			commandLists.push_back(pCurrentContext->GetCommandList());
		}
		pCurrentContext = pNextContext;
	}
	VERIFY_HR_EX(pCurrentContext->GetCommandList()->Close(), GetParent()->GetDevice());
	commandLists.push_back(pCurrentContext->GetCommandList());

	m_pCommandQueue->ExecuteCommandLists((uint32)commandLists.size(), commandLists.data());
	
	std::lock_guard<std::mutex> lock(m_FenceMutex);
	uint64 fenceValue = m_pFence->Signal(this);
	if (pBarrierCommandlist)
	{
		pBarrierCommandlist->Free(fenceValue);
	}
	return fenceValue;
}

ID3D12CommandAllocator* CommandQueue::RequestAllocator()
{
	std::scoped_lock<std::mutex> lock(m_AllocationMutex);
	if (m_FreeAllocators.empty() == false)
	{
		std::pair<ID3D12CommandAllocator*, uint64>& pFirst = m_FreeAllocators.front();
		if (m_pFence->IsComplete(pFirst.second))
		{
			m_FreeAllocators.pop();
			pFirst.first->Reset();
			return pFirst.first;
		}
	}

	RefCountPtr<ID3D12CommandAllocator> pAllocator;
	GetParent()->GetDevice()->CreateCommandAllocator(m_Type, IID_PPV_ARGS(pAllocator.GetAddressOf()));
	D3D::SetObjectName(pAllocator.Get(), Sprintf("Pooled Allocator %d - %s", (int)m_CommandAllocators.size(), D3D::CommandlistTypeToString(m_Type)).c_str());
	m_CommandAllocators.push_back(pAllocator);
	return m_CommandAllocators.back().Get();
}

void CommandQueue::FreeAllocator(uint64 fenceValue, ID3D12CommandAllocator* pAllocator)
{
	std::scoped_lock<std::mutex> lock(m_AllocationMutex);
	m_FreeAllocators.push(std::pair<ID3D12CommandAllocator*, uint64>(pAllocator, fenceValue));
}

void CommandQueue::InsertWait(uint64 fenceValue)
{
	m_pFence->GpuWait(this, fenceValue);
}

void CommandQueue::WaitForFence(uint64 fenceValue)
{
	m_pFence->CpuWait(fenceValue);
}

void CommandQueue::WaitForIdle()
{
	uint64 fenceValue = m_pFence->Signal(this);
	m_pFence->CpuWait(fenceValue);
}
