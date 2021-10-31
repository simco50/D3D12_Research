#include "stdafx.h"
#include "FileWatcher.h"

FileWatcher::FileWatcher()
{
}

FileWatcher::~FileWatcher()
{
	m_Exiting = true;
	m_Watches.clear();
	if (m_IOCP)
	{
		PostQueuedCompletionStatus(m_IOCP, 0, (ULONG_PTR)this, 0);
		CloseHandle(m_IOCP);
	}
}

bool FileWatcher::StartWatching(const char* pPath, const bool recursiveWatch /*= true*/)
{
	QueryPerformanceFrequency(&m_TimeFrequency);

	if (!Paths::DirectoryExists(pPath))
	{
		E_LOG(Warning, "FileWatch failed: Directory '%s' does not exist", pPath);
		return false;
	}

	std::string directoryPath = Paths::GetDirectoryPath(pPath);

	HANDLE fileHandle = CreateFileA(pPath,
		FILE_LIST_DIRECTORY,
		FILE_SHARE_WRITE | FILE_SHARE_READ | FILE_SHARE_DELETE,
		nullptr,
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
		nullptr);

	if (!fileHandle)
	{
		return false;
	}

	std::unique_ptr<DirectoryWatch> pWatch = std::make_unique<DirectoryWatch>();
	pWatch->Recursive = recursiveWatch;
	pWatch->FileHandle = fileHandle;
	m_IOCP = CreateIoCompletionPort(fileHandle, m_IOCP, (ULONG_PTR)pWatch.get(), 0);
	check(m_IOCP);
	m_Watches.push_back(std::move(pWatch));
	
	if (!m_Thread.IsRunning())
	{
		m_Thread.RunThread([](void* pArgs)
			{
				FileWatcher* pWatcher = (FileWatcher*)pArgs;
				return (DWORD)pWatcher->ThreadFunction();
			}, this);
	}

	check(PostQueuedCompletionStatus(m_IOCP, 0, (ULONG_PTR)this, 0) == TRUE);
	return true;
}

bool FileWatcher::GetNextChange(FileEvent& fileEvent)
{
	std::scoped_lock<std::mutex> lock(m_Mutex);
	for (auto& pWatch : m_Watches)
	{
		if (pWatch->Changes.size() > 0)
		{
			fileEvent = pWatch->Changes[0];
			LARGE_INTEGER currentTime;
			QueryPerformanceCounter(&currentTime);
			float timeDiff = ((float)currentTime.QuadPart - fileEvent.Time.QuadPart) / m_TimeFrequency.QuadPart;
			if (timeDiff > 0.02f)
			{
				pWatch->Changes.pop_front();
				return true;
			}
		}
	}
	return false;
}

int FileWatcher::ThreadFunction()
{
	const uint32 fileNotifyFlags =
		FILE_NOTIFY_CHANGE_LAST_WRITE |
		FILE_NOTIFY_CHANGE_SIZE |
		FILE_NOTIFY_CHANGE_CREATION |
		FILE_NOTIFY_CHANGE_FILE_NAME;

	while(!m_Exiting)
	{
		{
			std::scoped_lock<std::mutex> lock(m_Mutex);

			for (auto& pWatch : m_Watches)
			{
				if (pWatch && !pWatch->IsWatching)
				{
					DWORD bytesFilled = 0;
					ReadDirectoryChangesW(
						pWatch->FileHandle,
						pWatch->Buffer.data(),
						(DWORD)pWatch->Buffer.size(),
						pWatch->Recursive,
						fileNotifyFlags,
						&bytesFilled,
						&pWatch->Overlapped,
						nullptr);
					pWatch->IsWatching = true;
				}
			}
		}

		DWORD numBytes;
		OVERLAPPED* ov;
		ULONG_PTR key;
		while (GetQueuedCompletionStatus(m_IOCP, &numBytes, &key, &ov, INFINITE))
		{
			if ((void*)key == this && numBytes == 0)
			{
				break;
			}

			if (numBytes == 0)
			{
				continue;
			}

			std::scoped_lock<std::mutex> lock(m_Mutex);
			DirectoryWatch* pWatch = (DirectoryWatch*)key;

			unsigned offset = 0;
			char outString[MAX_PATH];
			while (offset < numBytes)
			{
				FILE_NOTIFY_INFORMATION* pRecord = (FILE_NOTIFY_INFORMATION*)&pWatch->Buffer[offset];

				int length = WideCharToMultiByte(
					CP_ACP,
					0,
					pRecord->FileName,
					pRecord->FileNameLength / sizeof(wchar_t),
					outString,
					MAX_PATH - 1,
					nullptr,
					nullptr
				);

				outString[length] = '\0';

				FileEvent newEvent;
				newEvent.Path = outString;

				switch (pRecord->Action)
				{
				case FILE_ACTION_MODIFIED: newEvent.EventType = FileEvent::Type::Modified; break;
				case FILE_ACTION_REMOVED: newEvent.EventType = FileEvent::Type::Removed; break;
				case FILE_ACTION_ADDED: newEvent.EventType = FileEvent::Type::Added; break;
				case FILE_ACTION_RENAMED_NEW_NAME: newEvent.EventType = FileEvent::Type::Added; break;
				case FILE_ACTION_RENAMED_OLD_NAME: newEvent.EventType = FileEvent::Type::Removed; break;
				}

				QueryPerformanceCounter(&newEvent.Time);

				bool add = true;

				//Some events are duplicates
				if (pWatch->Changes.size() > 0)
				{
					const FileEvent& prevEvent = pWatch->Changes.front();
					add = prevEvent.Path != newEvent.Path ||
						prevEvent.EventType != newEvent.EventType;
				}

				if (add)
				{
					pWatch->Changes.push_back(newEvent);
				}

				if (!pRecord->NextEntryOffset)
				{
					break;
				}
				else
				{
					offset += pRecord->NextEntryOffset;
				}
			}

			DWORD bytesFilled = 0;
			ReadDirectoryChangesW(
				pWatch->FileHandle,
				pWatch->Buffer.data(),
				(DWORD)pWatch->Buffer.size(),
				pWatch->Recursive,
				fileNotifyFlags,
				&bytesFilled,
				&pWatch->Overlapped,
				nullptr);
		}
	}

	return 0;
}

FileWatcher::DirectoryWatch::~DirectoryWatch()
{
	if (FileHandle)
	{
		CancelIo(FileHandle);
		CloseHandle(FileHandle);
		FileHandle = nullptr;
	}
}
