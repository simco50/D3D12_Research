#pragma once
#include "Stb/stb_sprintf.h"

inline int FormatString(char* pBuffer, int bufferSize, const char* pFormat, ...)
{
	va_list args;
	va_start(args, pFormat);
	int w = stbsp_vsnprintf(pBuffer, (int)bufferSize, pFormat, args);
	va_end(args);
	if (pBuffer == nullptr)
		return w;
	if (w == -1 || w >= (int)bufferSize)
		w = (int)bufferSize - 1;
	pBuffer[w] = 0;
	return w;
}

inline int FormatStringVars(char* pBuffer, size_t bufferSize, const char* pFormat, va_list args)
{
	int w = stbsp_vsnprintf(pBuffer, (int)bufferSize, pFormat, args);
	if (pBuffer == NULL)
		return w;
	if (w == -1 || w >= (int)bufferSize)
		w = (int)bufferSize - 1;
	pBuffer[w] = 0;
	return w;
}

template<typename... Args>
std::string Sprintf(const char* pFormat, Args... args)
{
	int length = FormatString(nullptr, 0, pFormat, args...);
	std::string str;
	str.resize(length);
	FormatString(str.data(), length + 1, pFormat, args...);
	return str;
}

namespace CharConv
{
	bool StrCmp(const char* pStrA, const char* pStrB, bool caseSensitive);

	inline void ToUpper(const char* pStr, char* pOut)
	{
		while (*pStr)
		{
			*pOut++ = (char)std::toupper(*pStr++);
		}
		*pOut = '\0';
	}

	inline void ToLower(const char* pStr, char* pOut)
	{
		while (*pStr)
		{
			*pOut++ = (char)std::tolower(*pStr++);
		}
		*pOut = '\0';
	}

	template<int I>
	int SplitString(const char* pStr, char(&buffer)[I], const char** pOut, int maxArgs, bool considerQuotes, char delimiter)
	{
		int num = 0;
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

	bool FromString(const char* pStr, char& out);
	bool FromString(const char* pStr, int& out);
	bool FromString(const char* pStr, float& out);
	bool FromString(const char* pStr, double& out);
	bool FromString(const char* pStr, const char*& pOut);
	bool FromString(const char* pStr, bool& out);

	template<typename T, int VALUES>
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
			if (!FromString(pArgs[i], pValue[i]))
			{
				return false;
			}
		}
		return true;
	}

	inline void ToString(char val, std::string* pOut) { *pOut = Sprintf("%c", val); }
	inline void ToString(int val, std::string* pOut) { *pOut = Sprintf("%d", val); }
	inline void ToString(float val, std::string* pOut) { *pOut = Sprintf("%.3f", val); }
	inline void ToString(double val, std::string* pOut) { *pOut = Sprintf("%.3f", val); }
	inline void ToString(const char* val, std::string* pOut) { *pOut = val; }
	inline void ToString(bool val, std::string* pOut) { *pOut = Sprintf("%d", val ? "True" : "False"); }

	/*
	template<> inline bool StrConvert(const char* pStr, Vector4& out) { return StrArrayConvert<float, 4>(pStr, &out.x); }
	template<> inline bool StrConvert(const char* pStr, Vector3& out) { return StrArrayConvert<float, 3>(pStr, &out.x); }
	template<> inline bool StrConvert(const char* pStr, Vector2& out) { return StrArrayConvert<float, 2>(pStr, &out.x); }
	template<> inline bool StrConvert(const char* pStr, IntVector2& out) { return StrArrayConvert<int, 2>(pStr, &out.x); }
	template<> inline bool StrConvert(const char* pStr, IntVector3& out) { return StrArrayConvert<int, 3>(pStr, &out.x); }
	*/

	/// /////////////////////////////////////////////////////////////////////////

	namespace Private
	{
		// INTERNAL: Create a tuple from string arguments
		template<size_t I, typename... Args>
		void TupleFromArguments(std::tuple<Args...>& t, const char** pArgs, int& failIndex)
		{
			if (failIndex == -1)
			{
				if (!CharConv::FromString(pArgs[I], std::get<I>(t)))
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
