#include "stdafx.h"
#include "FileWatcher.h"
#include <stdlib.h>

FileWatcher::FileWatcher()
{

}

FileWatcher::~FileWatcher()
{
	StopWatching();
}

bool FileWatcher::StartWatching(const std::string& directory, const bool recursiveWatch /*= true*/)
{
#if PLATFORM_WINDOWS
	if (m_Exiting)
	{
		QueryPerformanceFrequency(&m_TimeFrequency);
		m_FileHandle = CreateFileA(directory.c_str(),
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
	float timeDiff = ((float)currentTime.QuadPart - fileEvent.Time.QuadPart) / fileEvent.Time.QuadPart * 1000000;
	if (timeDiff < 100)
	{
		return false;
	}
	m_Changes.pop_front();
	return true;
}

int FileWatcher::ThreadFunction()
{
	while (!m_Exiting)
	{
		unsigned char buffer[BUFFERSIZE];
		DWORD bytesFilled = 0;
		if (ReadDirectoryChangesW(m_FileHandle,
			buffer,
			BUFFERSIZE,
			m_RecursiveWatch,
			FILE_NOTIFY_CHANGE_LAST_WRITE,
			&bytesFilled,
			nullptr,
			nullptr))
		{
			unsigned offset = 0;

			std::scoped_lock<std::mutex> lock(m_Mutex);
			while (offset < bytesFilled)
			{
				FILE_NOTIFY_INFORMATION* record = (FILE_NOTIFY_INFORMATION*)&buffer[offset];

				wchar_t target[256];
				memcpy(target, record->FileName, record->FileNameLength);
				target[record->FileNameLength / sizeof(wchar_t)] = L'\0';

				char cString[256];
				ToMultibyte(target, cString, 256);

				FileEvent newEvent;
				newEvent.Path = cString;

				switch (record->Action)
				{
				case FILE_ACTION_MODIFIED: newEvent.EventType = FileEvent::Type::Modified; break;
				case FILE_ACTION_REMOVED: newEvent.EventType = FileEvent::Type::Removed; break;
				case FILE_ACTION_ADDED: newEvent.EventType = FileEvent::Type::Added; break;
				case FILE_ACTION_RENAMED_NEW_NAME: newEvent.EventType = FileEvent::Type::Renamed; break;
				case FILE_ACTION_RENAMED_OLD_NAME: newEvent.EventType = FileEvent::Type::Renamed; break;
				}

				QueryPerformanceCounter(&newEvent.Time);

				m_Changes.push_back(newEvent);

				if (!record->NextEntryOffset)
				{
					break;
				}
				else
				{
					offset += record->NextEntryOffset;
				}
			}
		}
	}
	return 0;
}
