namespace CharConv
{
	bool StrCmp(const char* pStrA, const char* pStrB, bool caseSensitive);

	template<size_t I>
	uint32 SplitString(const char* pStr, char(&buffer)[I], const char** pOut, uint32 maxArgs, bool considerQuotes, char delimiter)
	{
		uint32 num = 0;
		bool delim = false;
		bool quoted = false;
		char* pData = buffer;
		memset(pData, 0, I);

		while (*pStr != '\0')
		{
			if (*pStr == '"' && considerQuotes)
			{
				quoted = !quoted;
			}
			else if (*pStr != delimiter || quoted)
			{
				*pData = *pStr;
				if (delim == false)
				{
					delim = true;
					if (num < maxArgs)
					{
						pOut[num] = pData;
					}
					++num;
				}
				++pData;
			}
			else
			{
				if (delim && !quoted)
				{
					*pData++ = '\0';
					delim = false;
				}
			}
			++pStr;
		}
		return num;
	}

	template<typename T>
	inline bool StrConvert(const char* pStr, T& out)
	{
		static_assert(false, "Not implemented.");
	}

	template<> bool StrConvert(const char* pStr, char& out);
	template<> bool StrConvert(const char* pStr, int& out);
	template<> bool StrConvert(const char* pStr, float& out);
	template<> bool StrConvert(const char* pStr, double& out);
	template<> bool StrConvert(const char* pStr, const char*& pOut);
	template<> bool StrConvert(const char* pStr, bool& out);

	template<typename T, size_t VALUES>
	bool StrArrayConvert(const char* pStr, T* pValue)
	{
		const char* pArgs[VALUES];
		char buffer[1024];
		int numValues = SplitString(pStr, buffer, &pArgs[0], VALUES, false, ',');
		if (numValues != VALUES)
		{
			return false;
		}
		for (int i = 0; i < VALUES; ++i)
		{
			if (!StrConvert(pArgs[i], pValue[i]))
			{
				return false;
			}
		}
		return true;
	}

	template<> inline bool StrConvert(const char* pStr, Vector4& out) { return StrArrayConvert<float, 4>(pStr, &out.x); }
	template<> inline bool StrConvert(const char* pStr, Vector3& out) { return StrArrayConvert<float, 3>(pStr, &out.x); }
	template<> inline bool StrConvert(const char* pStr, Vector2& out) { return StrArrayConvert<float, 2>(pStr, &out.x); }
	template<> inline bool StrConvert(const char* pStr, IntVector2& out) { return StrArrayConvert<int, 2>(pStr, &out.x); }
	template<> inline bool StrConvert(const char* pStr, IntVector3& out) { return StrArrayConvert<int, 3>(pStr, &out.x); }

	/// /////////////////////////////////////////////////////////////////////////

	namespace Private
	{
		// INTERNAL: Create a tuple from string arguments
		template<size_t I, typename... Args>
		void TupleFromArguments(std::tuple<Args...>& t, const char** pArgs, int& failIndex)
		{
			if (failIndex == -1)
			{
				if (!CharConv::StrConvert(pArgs[I], std::get<I>(t)))
				{
					failIndex = I;
				}
				if constexpr (I < sizeof...(Args) - 1)
				{
					TupleFromArguments<I + 1>(t, pArgs, failIndex);
				}
			}
		}
	}

	// Create a tuple from string arguments
	template<typename... Args>
	std::tuple<Args...> TupleFromArguments(const char** pArgs, int* pFailIndex)
	{
		std::tuple<Args...> pTuple;
		if constexpr (sizeof...(Args) > 0)
		{
			Private::TupleFromArguments<0>(pTuple, pArgs, *pFailIndex);
		}
		return pTuple;
	}
}
