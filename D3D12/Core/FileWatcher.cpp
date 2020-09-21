#include "stdafx.h"
#include "FileWatcher.h"

FileWatcher::FileWatcher()
{

}

FileWatcher::~FileWatcher()
{
	StopWatching();
}

bool FileWatcher::StartWatching(const std::string& directory, const bool recursiveWatch /*= true*/)
{
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
	return false;
}

void FileWatcher::StopWatching()
{
	m_Exiting = true;
	CancelIoEx(m_FileHandle, nullptr);
	CloseHandle(m_FileHandle);
}

bool FileWatcher::GetNextChange(std::string& fileName)
{
	std::scoped_lock<std::mutex> lock(m_Mutex);
	if (m_Changes.size() == 0)
	{
		return false;
	}
	const std::pair<std::string, LARGE_INTEGER>& entry = *m_Changes.begin();
	LARGE_INTEGER currentTime;
	QueryPerformanceCounter(&currentTime);
	float timeDiff = ((float)currentTime.QuadPart - entry.second.QuadPart) / entry.second.QuadPart * 1000000;
	if (timeDiff < 100)
	{
		return false;
	}
	fileName = entry.first;
	m_Changes.erase(fileName);
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

			while (offset < bytesFilled)
			{
				FILE_NOTIFY_INFORMATION* record = (FILE_NOTIFY_INFORMATION*)&buffer[offset];

				if (record->Action == FILE_ACTION_MODIFIED || record->Action == FILE_ACTION_RENAMED_NEW_NAME)
				{
					const wchar_t* src = record->FileName;
					const wchar_t* end = src + record->FileNameLength / 2;
					char fn[256];
					std::wcstombs(fn, src, 256);
					fn[end - src] = '\0';
					std::string filename(fn);
					std::replace(filename.begin(), filename.end(), '\\', '/');
					AddChange(filename);
				}

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

void FileWatcher::AddChange(const std::string& fileName)
{
	std::scoped_lock<std::mutex> lock(m_Mutex);
	QueryPerformanceCounter(&m_Changes[fileName]);
}
