#pragma once


class Serializer
{
public:
	static constexpr uint32 SerializerVersion = 0;

	enum class Mode
	{
		Read,
		Write,
	};

	bool Open(const char* pFilePath, Mode mode)
	{
		check(!m_File);
		if (fopen_s(&m_File, pFilePath, mode == Mode::Read ? "rb" : "wb") != 0)
			return false;

		m_Mode = mode;
		if (mode == Mode::Read)
		{
			Serialize(m_SerializerVersion);
			if (m_SerializerVersion != SerializerVersion)
			{
				Close();
				return false;
			}
		}
		else
		{
			m_SerializerVersion = SerializerVersion;
			Serialize(m_SerializerVersion);
		}
		return true;
	}

	void Close()
	{
		if (m_File)
			fclose(m_File);
	}

	bool IsOpen() const { return m_File != nullptr; }

	~Serializer()
	{
		Close();
	}

	template<typename T>
	void Serialize(T& v)
	{
		static_assert(std::is_trivially_copyable_v<T>);
		if (m_Mode == Mode::Read)
		{
			Read(&v, sizeof(T));
		}
		else
		{
			Write(&v, sizeof(T));
		}
	}

	template<typename T>
	void Serialize(std::vector<T>& arr)
	{
		if (m_Mode == Mode::Read)
		{
			uint32 size = 0;
			Serialize(size);
			arr.resize(size);
			for (T& e : arr)
				Serialize(e);
		}
		else
		{
			uint32 size = (uint32)arr.size();
			Serialize(size);
			for (T& e : arr)
				Serialize(e);
		}
	}

	void Serialize(std::string& str)
	{
		if (m_Mode == Mode::Read)
		{
			uint32 size = 0;
			Read(&size, sizeof(uint32));
			str.resize(size);
			Read(str.data(), size);
		}
		else
		{
			uint32 size = (uint32)str.size();
			Write(&size, sizeof(uint32));
			Write(str.data(), size);
		}
	}

	void Serialize(void*& pData, uint32& size)
	{
		if (m_Mode == Mode::Read)
		{
			check(!pData);
			Read(&size, sizeof(uint32));
			pData = new char[size];
			Read(pData, size);
		}
		else
		{
			check(pData);
			Write(&size, sizeof(uint32));
			Write(pData, size);
		}
	}

	void Move(int offset)
	{
		check(m_File);
		fseek(m_File, offset, SEEK_CUR);
	}

	template<typename T>
	Serializer& operator|=(T& rhs)
	{
		Serialize(rhs);
		return *this;
	}

private:
	void Write(const void* pData, uint32 size)
	{
		check(m_File);
		fwrite(pData, 1, size, m_File);
	}

	void Read(void* pData, uint32 size)
	{
		check(m_File);
		fread(pData, 1, size, m_File);
	}

	FILE* m_File = nullptr;
	Mode m_Mode = Mode::Read;
	uint32 m_SerializerVersion = 0xFFFFFFFF;
};
