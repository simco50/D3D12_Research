#include "stdafx.h"
#include "CommandQueue.h"
#include "CommandAllocatorPool.h"
#include "Graphics.h"

CommandQueue::CommandQueue(ID3D12Device* pDevice, D3D12_COMMAND_LIST_TYPE type)
{
	D3D12_COMMAND_QUEUE_DESC desc = {};
	desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	desc.NodeMask = 0;
	desc.Priority = 0;
	m_Type = type;

	HR(pDevice->CreateCommandQueue(&desc, IID_PPV_ARGS(m_pCommandQueue.GetAddressOf())));
	HR(pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_pFence.GetAddressOf())));
	m_pFence->Signal(m_LastCompletedFenceValue);

	m_pFenceEventHandle = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);

	m_pAllocatorPool = std::make_unique<CommandAllocatorPool>(pDevice, m_Type);
}

CommandQueue::~CommandQueue()
{
	CloseHandle(m_pFenceEventHandle);
}

unsigned long long CommandQueue::ExecuteCommandList(CommandContext* pCommandList, bool waitForCompletion)
{
	HR(pCommandList->pCommandList->Close());
	ID3D12CommandList* pCommandLists[] = { pCommandList->pCommandList };
	m_pCommandQueue->ExecuteCommandLists(1, pCommandLists);
	std::lock_guard<std::mutex> lock(m_FenceMutex);
	m_pCommandQueue->Signal(m_pFence.Get(), m_NextFenceValue);
	m_pAllocatorPool->FreeAllocator(pCommandList->pAllocator, m_NextFenceValue);
	if (waitForCompletion)
	{
		WaitForFenceBlock(m_NextFenceValue);
	}
	return m_NextFenceValue++;
}

bool CommandQueue::IsFenceComplete(unsigned long long fenceValue)
{
	if (fenceValue > m_LastCompletedFenceValue)
	{
		m_LastCompletedFenceValue = std::max(m_LastCompletedFenceValue, m_pFence->GetCompletedValue());
	}

	return fenceValue <= m_LastCompletedFenceValue;
}

void CommandQueue::InsertWait(unsigned long long fenceValue)
{
	m_pCommandQueue->Wait(m_pFence.Get(), fenceValue);
}

void CommandQueue::InsertWaitForQueueFence(CommandQueue* pQueue, unsigned long long fenceValue)
{
	m_pCommandQueue->Wait(pQueue->GetFence(), fenceValue);
}

void CommandQueue::InsertWaitForQueue(CommandQueue* pQueue)
{
	m_pCommandQueue->Wait(pQueue->GetFence(), pQueue->GetNextFenceValue() - 1);
}

unsigned long long CommandQueue::IncrementFence()
{
	std::lock_guard<std::mutex> LockGuard(m_FenceMutex);
	m_pCommandQueue->Signal(m_pFence.Get(), m_NextFenceValue);
	return m_NextFenceValue++;
}

void CommandQueue::WaitForFenceBlock(unsigned long long fenceValue)
{
	if (IsFenceComplete(fenceValue))
	{
		return;
	}

	{
		std::lock_guard<std::mutex> lockGuard(m_EventMutex);

		m_pFence->SetEventOnCompletion(fenceValue, m_pFenceEventHandle);
		WaitForSingleObject(m_pFenceEventHandle, INFINITE);
		m_LastCompletedFenceValue = fenceValue;
	}
}

void CommandQueue::WaitForIdle()
{
	WaitForFenceBlock(IncrementFence());
}

unsigned long long CommandQueue::PollCurrentFenceValue()
{
	m_LastCompletedFenceValue = max(m_LastCompletedFenceValue, m_pFence->GetCompletedValue());
	return m_LastCompletedFenceValue;
}
