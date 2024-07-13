#include "stdafx.h"
#include "Stream.h"


bool Stream::ReadLine(char* pOutStr, uint32 maxLength)
{
	uint32 length = 0;
	while (GetCursor() < GetLength())
	{
		if (length + 1 > maxLength)
			return false;

		char c;
		Read(&c, sizeof(char));
		if (c == '\r')
			continue;

		if (c == '\n')
		{
			pOutStr[length++] = 0;
			return true;
		}

		pOutStr[length++] = c;
	}
	if (length > 0)
		pOutStr[length++] = 0;
	return length > 0;
}



MemoryStream::MemoryStream(bool isWriting, const void* pMemory, uint32 size)
	: Stream(isWriting)
{
	SetBuffer(pMemory, size);
}

MemoryStream::~MemoryStream()
{
	if (IsWriting())
		delete[] m_pDataBase;
}

bool MemoryStream::Write(const void* pData, uint32 size)
{
	assert(IsWriting());
	EnsureBufferSize(size);
	memcpy(m_pData, pData, size);
	m_pData += size;
	return true;
}

void MemoryStream::Seek(int offset, StreamSeekMode mode)
{
	if (mode == StreamSeekMode::Absolute)
	{
		gAssert((uint32)offset < GetLength());
		m_pData = m_pDataBase + offset;
	}
	else if (mode == StreamSeekMode::Relative)
	{
		gAssert(GetCursor() + offset <= GetLength());
		m_pData += offset;
	}
}

bool MemoryStream::Read(void* pData, uint32 size, uint32* pRead)
{
	gAssert(GetCursor() + size <= GetLength());

	memcpy(pData, m_pData, size);
	m_pData += size;
	if (pRead)
		*pRead = size;
	return true;
}

void MemoryStream::SetBuffer(const void* pBuffer, uint32 length)
{
	if (pBuffer)
	{
		m_pDataBase = (char*)pBuffer;
		m_pData = m_pDataBase;
		m_Capacity = length;
	}
	else
	{
		m_pDataBase = nullptr;
		m_pData = nullptr;
		m_Capacity = 0;
	}
}

void MemoryStream::SetLength(uint32 length)
{
	uint32 pos = GetCursor();

	char* pNewData = new char[length];
	if (m_pDataBase)
	{
		memcpy(pNewData, m_pDataBase, GetCursor());
		delete[] m_pDataBase;
	}

	m_pDataBase = pNewData;
	m_pData = m_pDataBase + pos;
	m_Capacity = length;
}

void MemoryStream::EnsureBufferSize(uint32 length)
{
	if (length <= m_Capacity)
		return;

	uint32 newCapacity = 256;
	while (length > newCapacity)
		newCapacity *= 2;

	SetLength(length);
}



FileStream::FileStream()
	: Stream(false)
{}

FileStream::~FileStream()
{
	Close();
}

bool FileStream::Open(const char* pFile, FileMode mode)
{
	Close();

	uint32 access = 0;
	if (mode & FileMode::Read)
		access |= GENERIC_READ;
	if (mode & FileMode::Write)
		access |= GENERIC_WRITE;

	uint32 disposition = 0;
	if (mode & FileMode::Create)
		disposition = CREATE_ALWAYS;
	else if (mode & FileMode::Write)
		disposition = OPEN_ALWAYS;
	else
		disposition = OPEN_EXISTING;

	m_pFile = ::CreateFileA(pFile, access, 0, nullptr, disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (m_pFile != INVALID_HANDLE_VALUE)
	{
		m_Length = ::GetFileSize(m_pFile, nullptr);
		m_Position = 0;
		m_Mode = mode;
	}
	return m_pFile != INVALID_HANDLE_VALUE;
}

bool FileStream::Close()
{
	if (IsOpen())
	{
		CloseHandle(m_pFile);
		m_pFile = nullptr;
	}
	return true;
}

bool FileStream::Flush()
{
	gAssert(IsOpen());

	if ((m_Mode & FileMode::Write) != FileMode::Write)
		return false;
	gAssert(IsOpen());
	return ::FlushFileBuffers(m_pFile) == TRUE;
}

bool FileStream::Write(const void* pData, uint32 size)
{
	gAssert(IsOpen());
	gAssert(m_Mode & FileMode::Write);

	DWORD written = 0;
	BOOL result = ::WriteFile(m_pFile, pData, size, &written, nullptr);
	m_Position += written;
	return result == TRUE;
}

bool FileStream::Read(void* pData, uint32 size, uint32* pRead)
{
	gAssert(IsOpen());
	gAssert(m_Mode & FileMode::Read);

	DWORD read = 0;
	BOOL result = ::ReadFile(m_pFile, pData, size, &read, nullptr);
	if (pRead)
		*pRead = (uint32)read;
	m_Position += read;
	return result == TRUE;
}

void FileStream::Seek(int offset, StreamSeekMode mode)
{
	gAssert(IsOpen());
	if (mode == StreamSeekMode::Absolute)
	{
		::SetFilePointer(m_pFile, offset, nullptr, FILE_BEGIN);
		m_Position = offset;
	}
	else if (mode == StreamSeekMode::Relative)
	{
		::SetFilePointer(m_pFile, offset, nullptr, FILE_CURRENT);
		m_Position += offset;
	}
}
