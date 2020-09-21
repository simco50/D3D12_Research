#pragma once
#include "Thread.h"

class FileWatcher
{
public:
	FileWatcher();
	virtual ~FileWatcher();

	bool StartWatching(const std::string& directory, const bool recursiveWatch = true);
	void StopWatching();
	bool GetNextChange(std::string& fileName);

private:
	int ThreadFunction();
	void AddChange(const std::string& fileName);

	static const int BUFFERSIZE = 2048;
	bool m_Exiting = true;
	bool m_RecursiveWatch = true;
	std::mutex m_Mutex;
	HANDLE m_FileHandle = nullptr;
	LARGE_INTEGER m_TimeFrequency;
	std::map<std::string, LARGE_INTEGER> m_Changes;
	Thread m_Thread;
};