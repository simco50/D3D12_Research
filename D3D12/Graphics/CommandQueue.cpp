#include "stdafx.h"
#include "CommandQueue.h"
#include "CommandAllocatorPool.h"
#include "Graphics.h"
#define USE_PIX
#include "pix3.h"

CommandQueue::CommandQueue(Graphics* pGraphics, D3D12_COMMAND_LIST_TYPE type)
	: GraphicsObject(pGraphics),
	m_NextFenceValue((uint64)type << 56 | 1),
	m_LastCompletedFenceValue((uint64)type << 56),
	m_Type(type)
{
	m_pAllocatorPool = std::make_unique<CommandAllocatorPool>(pGraphics, type);

	D3D12_COMMAND_QUEUE_DESC desc = {};
	desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	desc.NodeMask = 0;
	desc.Priority = 0;
	desc.Type = type;

	HR(pGraphics->GetDevice()->CreateCommandQueue(&desc, IID_PPV_ARGS(m_pCommandQueue.GetAddressOf())));
	HR(pGraphics->GetDevice()->CreateFence(m_LastCompletedFenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_pFence.GetAddressOf())));

	m_pFenceEventHandle = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
}

CommandQueue::~CommandQueue()
{
	CloseHandle(m_pFenceEventHandle);
}

uint64 CommandQueue::ExecuteCommandList(ID3D12CommandList* pCommandList)
{
	std::lock_guard<std::mutex> lock(m_FenceMutex);
	HR(static_cast<ID3D12GraphicsCommandList*>(pCommandList)->Close());
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