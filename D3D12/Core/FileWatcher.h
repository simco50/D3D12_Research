#pragma once
#include "Thread.h"

class FileWatcher
{
public:
	struct FileEvent
	{
		enum class Type
		{
			Modified,
			Renamed,
			Removed,
			Added,
		};
		Type EventType;
		std::string Path;
		LARGE_INTEGER Time;
	};

	FileWatcher();
	virtual ~FileWatcher();

	bool StartWatching(const std::string& directory, const bool recursiveWatch = true);
	void StopWatching();
	bool GetNextChange(FileEvent& fileFileEvent);

private:
	int ThreadFunction();

	static const int BUFFERSIZE = 2048;
	bool m_Exiting = true;
	bool m_RecursiveWatch = true;
	std::mutex m_Mutex;
	HANDLE m_FileHandle = nullptr;
	LARGE_INTEGER m_TimeFrequency;
	std::deque<FileEvent> m_Changes;
	Thread m_Thread;
};
