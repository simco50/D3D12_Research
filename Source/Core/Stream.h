#pragma once

enum class StreamSeekMode
{
	Absolute,
	Relative,
};

class Stream
{
public:
	Stream(bool isWriting)
		: m_IsWriting(isWriting)
	{}

	virtual ~Stream() = default;

	virtual bool Write(const void* pData, uint32 size) = 0;
	virtual bool Read(void* pData, uint32 size, uint32* pRead = nullptr) = 0;

	bool ReadLine(char* pOutStr, uint32 maxLength);

	template<typename T>
	T Read()
	{
		T value;
		Read(&value, sizeof(T));
		return value;
	}

	virtual uint32	GetLength() const = 0;
	virtual void	Seek(int offset, StreamSeekMode mode) = 0;
	virtual uint32	GetCursor() const = 0;
	virtual bool	Flush() { return true; }

	bool IsWriting() const { return m_IsWriting; }
	bool IsReading() const { return !m_IsWriting; }

protected:
	bool m_IsWriting;
};


/*
	Write operators
*/

inline Stream& operator<<(Stream& stream, uint32 value) { stream.Write(&value, sizeof(uint32)); return stream; }
inline Stream& operator<<(Stream& stream, uint64 value)	{ stream.Write(&value, sizeof(uint64)); return stream; }
inline Stream& operator<<(Stream& stream, int value)	{ stream.Write(&value, sizeof(int));	return stream; }
inline Stream& operator<<(Stream& stream, char value)	{ stream.Write(&value, sizeof(char));	return stream; }
inline Stream& operator<<(Stream& stream, float value)	{ stream.Write(&value, sizeof(float));	return stream; }
inline Stream& operator<<(Stream& stream, const String value)
{
	stream << (uint32)value.length();
	stream.Write(&value[0], (uint32)value.length());
	return stream;
}

template<typename T>
inline Stream& operator<<(Stream& stream, const Array<T>& value)
{
	stream << (uint32)value.size();
	for (const T& v : value)
		stream << v;
	return stream;
}

template<typename T, uint32 N>
inline Stream& operator<<(Stream& stream, const T (&value)[N])
{
	stream << N;
	for (const T& v : value)
		stream << v;
	return stream;
}

/*
	Read operators
*/

inline Stream& operator>>(Stream& stream, uint32& value) { stream.Read(&value, sizeof(uint32)); return stream; }
inline Stream& operator>>(Stream& stream, uint64& value) { stream.Read(&value, sizeof(uint64)); return stream; }
inline Stream& operator>>(Stream& stream, int& value) { stream.Read(&value, sizeof(int)); return stream; }
inline Stream& operator>>(Stream& stream, char& value) { stream.Read(&value, sizeof(char)); return stream; }
inline Stream& operator>>(Stream& stream, float& value) { stream.Read(&value, sizeof(float)); return stream; }
inline Stream& operator>>(Stream& stream, String& value)
{
	uint32 len = 0;
	stream >> len;
	value.resize(len);
	stream.Read(&value[0], len);
	return stream;
}

template<typename T>
inline Stream& operator>>(Stream& stream, Array<T>& value)
{
	uint32 size = 0;
	stream >> size;
	value.resize(size);
	for (T& v : value)
		stream >> v;
	return stream;
}

template<typename T, uint32 N>
inline Stream& operator>>(Stream& stream, T(&value)[N])
{
	uint32 size = 0;
	stream >> size;
	check(size <= N);
	for (uint32 i = 0; i < size; ++i)
		stream >> value[i];
	return stream;
}



class MemoryStream : public Stream
{
public:
	MemoryStream(bool isWriting, const void* pMemory = nullptr, uint32 size = 0);
	~MemoryStream();

	bool	Write(const void* pData, uint32 size) override;
	void	Seek(int offset, StreamSeekMode mode) override;
	bool	Read(void* pData, uint32 size, uint32* pRead = nullptr) override;

	void	SetBuffer(const void* pBuffer, uint32 length);
	void	SetLength(uint32 length);

	uint32	GetCursor() const override	{ return m_pData ? (uint32)(m_pData - m_pDataBase) : 0; }
	uint32	GetLength() const override	{ return m_Capacity; }
	void*	GetData() const				{ return m_pDataBase; }

private:
	void	EnsureBufferSize(uint32 length);

	char* m_pDataBase = nullptr;
	char* m_pData = nullptr;
	uint32 m_Capacity;
};



enum FileMode
{
	None	= 0,
	Read	= 1 << 0,
	Write	= 1 << 1,
	Create	= 1 << 2,
};
DECLARE_BITMASK_TYPE(FileMode)

class FileStream : public Stream
{
public:
	FileStream();
	~FileStream();

	bool Open(const char* pFile, FileMode mode);
	bool Close();

	bool Flush() override;
	bool Write(const void* pData, uint32 size) override;
	bool Read(void* pData, uint32 size, uint32* pRead = nullptr) override;
	void Seek(int offset, StreamSeekMode mode) override;

	uint32 GetLength() const override	{ return m_Length; }
	uint32 GetCursor() const override	{ return m_Position; }
	bool IsOpen() const					{ return m_pFile != nullptr; }

private:
	HANDLE m_pFile		= nullptr;
	uint32 m_Length		= 0;
	uint32 m_Position	= 0;
	FileMode m_Mode		= FileMode::None;
};
