#include "stdafx.h"
#include "Fence.h"
#include "CommandQueue.h"
#include "D3D.h"
#include "Graphics.h"

Fence::Fence(GraphicsDevice* pParent, const char* pName, uint64 fenceValue)
	: GraphicsObject(pParent), m_CurrentValue(fenceValue + 1), m_LastSignaled(0), m_LastCompleted(fenceValue)
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

uint64 Fence::Signal(uint64 fenceValue)
{
	m_LastSignaled = fenceValue;
	m_LastCompleted = fenceValue;
	m_CurrentValue++;
	return m_LastSignaled;
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

#ifdef USE_PIX
	// The event was successfully signaled, so notify PIX
	if (result == WAIT_OBJECT_0)
	{
		PIXNotifyWakeFromFenceSignal(m_CompleteEvent);
	}
#else
	UNREFERENCED_PARAMETER(result);
#endif

	m_LastCompleted = m_pFence->GetCompletedValue();
}

void Fence::CpuWait()
{
	CpuWait(m_LastSignaled);
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
