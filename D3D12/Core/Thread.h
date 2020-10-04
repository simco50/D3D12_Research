#pragma once

class Thread
{
public:
	using ThreadFunction = DWORD(__stdcall *)(void*);

	Thread();
	~Thread();

	bool SetPriority(const int priority);
	void SetAffinity(const uint64 affinity);
	void LockToCore(const uint32 core);
	void SetName(const std::string& name);

	static void SetCurrentAffinity(const uint64 affinity);
	static void LockCurrentToCore(const uint32 core);

	//Get the given thread ID
	unsigned long GetId() const { return m_ThreadId; }
	bool IsCurrentThread() const { return GetId() == GetCurrentId(); }
	static uint32 GetCurrentId();
	bool IsRunning() const { return m_pHandle != nullptr; }

	static void SetMainThread();
	static bool IsMainThread();
	static bool IsMainThread(uint32 id);

	bool RunThread(ThreadFunction function, void* pArgs);
	void StopThread();

private:
	static void SetAffinity(void* pHandle, const uint64 affinity);

	static unsigned int m_MainThread;
	uint32 m_ThreadId = 0;
	void* m_pHandle = nullptr;
};