#pragma once

class RWMutex
{
public:
	RWMutex()
	{
		InitializeSRWLock(&m_Lock);
	}

	void LockWrite()
	{
		AcquireSRWLockExclusive(&m_Lock);
	}

	void UnlockWrite()
	{
		ReleaseSRWLockExclusive(&m_Lock);
	}

	void LockRead()
	{
		AcquireSRWLockShared(&m_Lock);
	}

	void UnlockRead()
	{
		ReleaseSRWLockShared(&m_Lock);
	}

private:
	SRWLOCK m_Lock;
};

class ScopedWriteLock
{
public:
	ScopedWriteLock(RWMutex& lock)
		: m_Lock(lock)
	{
		lock.LockWrite();
	}

	~ScopedWriteLock()
	{
		m_Lock.UnlockWrite();
	}

private:
	RWMutex& m_Lock;
};


class ScopedReadLock
{
public:
	ScopedReadLock(RWMutex& lock)
		: m_Lock(lock)
	{
		lock.LockRead();
	}

	~ScopedReadLock()
	{
		m_Lock.UnlockRead();
	}

private:
	RWMutex& m_Lock;
};
