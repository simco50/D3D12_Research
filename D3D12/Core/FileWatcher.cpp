#include "stdafx.h"
#include "FileWatcher.h"

FileWatcher::FileWatcher()
{

}

FileWatcher::~FileWatcher()
{
	StopWatching();
}

bool FileWatcher::StartWatching(const char* pDirectory, const bool recursiveWatch /*= true*/)
{
#if PLATFORM_WINDOWS
	if (m_Exiting)
	{
		QueryPerformanceFrequency(&m_TimeFrequency);
		m_FileHandle = CreateFileA(pDirectory,
			FILE_LIST_DIRECTORY,
			FILE_SHARE_WRITE | FILE_SHARE_READ | FILE_SHARE_DELETE,
			nullptr,
			OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS,
			nullptr);
		if (m_FileHandle != INVALID_HANDLE_VALUE)
		{
			m_RecursiveWatch = recursiveWatch;
			m_Exiting = false;
			m_Thread.RunThread([](void* pArgs) 
				{ 
					FileWatcher* pWatcher = (FileWatcher*)pArgs;
					return (DWORD)pWatcher->ThreadFunction();
				}, this);
		}
		return false;
	}
#endif
	return false;
}

void FileWatcher::StopWatching()
{
	if (!m_Exiting)
	{
		m_Exiting = true;
		CancelIoEx(m_FileHandle, nullptr);
		CloseHandle(m_FileHandle);
	}
}

bool FileWatcher::GetNextChange(FileEvent& fileEvent)
{
	std::scoped_lock<std::mutex> lock(m_Mutex);
	if (m_Changes.size() == 0)
	{
		return false;
	}
	fileEvent = m_Changes[0];
	LARGE_INTEGER currentTime;
	QueryPerformanceCounter(&currentTime);
	float timeDiff = ((float)currentTime.QuadPart - fileEvent.Time.QuadPart) / m_TimeFrequency.QuadPart;
	if (timeDiff < 0.02f)
	{
		return false;
	}
	m_Changes.pop_front();
	return true;
}

int FileWatcher::ThreadFunction()
{
	const uint32 fileNotifyFlags =
		FILE_NOTIFY_CHANGE_LAST_WRITE |
		FILE_NOTIFY_CHANGE_SIZE |
		FILE_NOTIFY_CHANGE_CREATION |
		FILE_NOTIFY_CHANGE_FILE_NAME;

	while (!m_Exiting)
	{
		uint8 buffer[1 << 12];
		DWORD bytesFilled = 0;
		if (ReadDirectoryChangesW(
			m_FileHandle,
			buffer,
			sizeof(buffer),
			m_RecursiveWatch,
			fileNotifyFlags,
			&bytesFilled,
			nullptr,
			nullptr))
		{

			std::scoped_lock<std::mutex> lock(m_Mutex);

			unsigned offset = 0;
			char outString[MAX_PATH];
			while (offset < bytesFilled)
			{
				FILE_NOTIFY_INFORMATION* pRecord = (FILE_NOTIFY_INFORMATION*)&buffer[offset];

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
				if (m_Changes.size() > 0)
				{
					const FileEvent& prevEvent = m_Changes.front();
					add = prevEvent.Path != newEvent.Path ||
						prevEvent.EventType != newEvent.EventType;
				}

				if (add)
				{
					m_Changes.push_back(newEvent);
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
		}
	}
	return 0;
}
