#pragma once
#include "Thread.h"

struct FileEvent
{
	enum class Type
	{
		Modified,
		Removed,
		Added,
	};
	Type EventType;
	std::string Path;
	LARGE_INTEGER Time;
};

class FileWatcher
{
public:
	FileWatcher();
	~FileWatcher();

	bool StartWatching(const char* pPath, const bool recursiveWatch = true);
	bool GetNextChange(FileEvent& fileFileEvent);

private:
	struct DirectoryWatch
	{
		~DirectoryWatch();
		bool IsWatching = false;
		bool Recursive;
		HANDLE FileHandle;
		OVERLAPPED Overlapped{};
		std::deque<FileEvent> Changes;
		std::array<char, 1 << 16> Buffer{};
		std::string SpecificFilePath;
	};

	int ThreadFunction();

	HANDLE m_IOCP = nullptr;
	bool m_Exiting = false;
	std::mutex m_Mutex;
	LARGE_INTEGER m_TimeFrequency{};
	Thread m_Thread;
	std::vector<std::unique_ptr<DirectoryWatch>> m_Watches;
};
