#include "stdafx.h"
#include "CommandQueue.h"
#include "Graphics.h"
#define USE_PIX
#include "pix3.h"

CommandAllocatorPool::CommandAllocatorPool(Graphics* pGraphics, D3D12_COMMAND_LIST_TYPE type)
	: GraphicsObject(pGraphics), m_Type(type)
{
}

ID3D12CommandAllocator* CommandAllocatorPool::GetAllocator(uint64 fenceValue)
{
	if (m_FreeAllocators.empty() == false)
	{
		std::pair<ID3D12CommandAllocator*, uint64>& pFirst = m_FreeAllocators.front();
		if (pFirst.second <= fenceValue)
		{
			m_FreeAllocators.pop();
			pFirst.first->Reset();
			return pFirst.first;
		}
	}

	ComPtr<ID3D12CommandAllocator> pAllocator;
	m_pGraphics->GetDevice()->CreateCommandAllocator(m_Type, IID_PPV_ARGS(pAllocator.GetAddressOf()));
	m_CommandAllocators.push_back(std::move(pAllocator));
	return m_CommandAllocators.back().Get();
}

void CommandAllocatorPool::FreeAllocator(ID3D12CommandAllocator* pAllocator, uint64 fenceValue)
{
	m_FreeAllocators.push(std::pair<ID3D12CommandAllocator*, uint64>(pAllocator, fenceValue));
}

CommandQueue::CommandQueue(Graphics* pGraphics, D3D12_COMMAND_LIST_TYPE type)
	: GraphicsObject(pGraphics),
	m_NextFenceValue((uint64)type << 56 | 1),
	m_LastCompletedFenceValue((uint64)type << 56)
{
	m_pAllocatorPool = std::make_unique<CommandAllocatorPool>(pGraphics, type);

	D3D12_COMMAND_QUEUE_DESC desc = {};
	desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	desc.NodeMask = 0;
	desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	desc.Type = type;

	VERIFY_HR_EX(pGraphics->GetDevice()->CreateCommandQueue(&desc, IID_PPV_ARGS(m_pCommandQueue.GetAddressOf())), m_pGraphics->GetDevice());
	VERIFY_HR_EX(pGraphics->GetDevice()->CreateFence(m_LastCompletedFenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_pFence.GetAddressOf())), m_pGraphics->GetDevice());

	m_pFenceEventHandle = CreateEventExA(nullptr, "CommandQueue Fence", 0, EVENT_ALL_ACCESS);
}

CommandQueue::~CommandQueue()
{
	CloseHandle(m_pFenceEventHandle);
}

uint64 CommandQueue::ExecuteCommandList(ID3D12CommandList* pCommandList)
{
	std::lock_guard<std::mutex> lock(m_FenceMutex);
	VERIFY_HR_EX(static_cast<ID3D12GraphicsCommandList*>(pCommandList)->Close(), m_pGraphics->GetDevice());
	m_pCommandQueue->ExecuteCommandLists(1, &pCommandList);
	m_pCommandQueue->Signal(m_pFence.Get(), m_NextFenceValue);
	return m_NextFenceValue++;
}

bool CommandQueue::IsFenceComplete(uint64 fenceValue)
{
	if (fenceValue > m_LastCompletedFenceValue)
	{
		m_LastCompletedFenceValue = std::max(m_LastCompletedFenceValue, m_pFence->GetCompletedValue());
	}

	return fenceValue <= m_LastCompletedFenceValue;
}

void CommandQueue::InsertWaitForFence(uint64 fenceValue)
{
	CommandQueue* pFenceValueOwner = m_pGraphics->GetCommandQueue((D3D12_COMMAND_LIST_TYPE)(fenceValue >> 56));
	m_pCommandQueue->Wait(pFenceValueOwner->GetFence(), fenceValue);
}

void CommandQueue::InsertWaitForQueue(CommandQueue* pQueue)
{
	m_pCommandQueue->Wait(pQueue->GetFence(), pQueue->GetNextFenceValue() - 1);
}

uint64 CommandQueue::IncrementFence()
{
	std::lock_guard<std::mutex> LockGuard(m_FenceMutex);
	m_pCommandQueue->Signal(m_pFence.Get(), m_NextFenceValue);
	return m_NextFenceValue++;
}

ID3D12CommandAllocator* CommandQueue::RequestAllocator()
{
	uint64 completedFence = m_pFence->GetCompletedValue();
	return m_pAllocatorPool->GetAllocator(completedFence);
}

void CommandQueue::FreeAllocator(uint64 fenceValue, ID3D12CommandAllocator* pAllocator)
{
	m_pAllocatorPool->FreeAllocator(pAllocator, fenceValue);
}

void CommandQueue::WaitForFence(uint64 fenceValue)
{
	if (IsFenceComplete(fenceValue))
	{
		return;
	}

	std::lock_guard<std::mutex> lockGuard(m_EventMutex);

	m_pFence->SetEventOnCompletion(fenceValue, m_pFenceEventHandle);
	DWORD result = WaitForSingleObject(m_pFenceEventHandle, INFINITE);

	switch (result)
	{
	case WAIT_OBJECT_0:
		PIXNotifyWakeFromFenceSignal(m_pFenceEventHandle); // The event was successfully signaled, so notify PIX
		break;
	}

	m_LastCompletedFenceValue = fenceValue;
}

void CommandQueue::WaitForIdle()
{
	WaitForFence(IncrementFence());
}