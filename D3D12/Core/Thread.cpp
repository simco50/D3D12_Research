#include "stdafx.h"
#include "Thread.h"

unsigned int Thread::m_MainThread = 0;

Thread::Thread()
{
}

Thread::~Thread()
{
	StopThread();
}

bool Thread::RunThread(ThreadFunction function, void* pArgs)
{
	if (m_pHandle)
	{
		return false;
	}
	m_pHandle = CreateThread(nullptr, 0, function, pArgs, 0, (DWORD*)&m_ThreadId);
	if (m_pHandle == nullptr)
	{
		auto error = GetLastError();
		return false;
	}
	return true;
}

void Thread::StopThread()
{
	if (!m_pHandle)
	{
		return;
	}
	WaitForSingleObject((HANDLE)m_pHandle, INFINITE);
	if (CloseHandle((HANDLE)m_pHandle) == 0)
	{
		auto error = GetLastError();
	}
	m_pHandle = nullptr;
}

bool Thread::SetPriority(const int priority)
{
	if (m_pHandle)
	{
		if (SetThreadPriority((HANDLE)m_pHandle, priority) == 0)
		{
			auto error = GetLastError();
			return false;
		}
		return true;
	}
	return false;
}

void Thread::SetAffinity(const uint64 affinity)
{
	SetAffinity(m_pHandle, affinity);
}

void Thread::SetAffinity(void* pHandle, const uint64 affinity)
{
	check(pHandle);
	::SetThreadAffinityMask((HANDLE*)pHandle, (DWORD)affinity);
}

void Thread::LockToCore(const uint32 core)
{
	uint32 affinity = 1 << core;
	SetAffinity(m_pHandle, (uint64)affinity);
}

void Thread::SetCurrentAffinity(const uint64 affinity)
{
	SetAffinity(GetCurrentThread(), affinity);
}

void Thread::LockCurrentToCore(const uint32 core)
{
	uint32 affinity = 1 << core;
	SetAffinity(GetCurrentThread(), (uint64)affinity);
}

uint32 Thread::GetCurrentId()
{
	return (uint32)::GetCurrentThreadId();
}

void Thread::SetMainThread()
{
	m_MainThread = GetCurrentId();
}

bool Thread::IsMainThread()
{
	return m_MainThread == GetCurrentId();
}

bool Thread::IsMainThread(uint32 id)
{
	return m_MainThread == id;
}

void Thread::SetName(const std::string& name)
{
	wchar_t wide[256];
	ToWidechar(name.c_str(), wide, 256);
	SetThreadDescription((HANDLE)m_pHandle, wide);
}
