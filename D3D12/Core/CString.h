#pragma once
#include "stb_sprintf.h"

template<typename CharSource, typename CharDest>
inline size_t StringConvert(const CharSource* pSource, CharDest* pDestination, int destinationSize);

template<>
inline size_t StringConvert(const wchar_t* pSource, char* pDestination, int destinationSize)
{
	size_t converted = 0;
	wcstombs_s(&converted, pDestination, destinationSize, pSource, destinationSize);
	return converted;
}

template<>
inline size_t StringConvert(const char* pSource, wchar_t* pDestination, int destinationSize)
{
	size_t converted = 0;
	mbstowcs_s(&converted, pDestination, destinationSize, pSource, destinationSize);
	return converted;
}

template<typename CharSource, typename CharDest>
struct StringConverter
{
	StringConverter(const CharSource* pStr)
		: m_String{}
	{
		StringConvert<CharSource, CharDest>(pStr, m_String, 128);
	}

	CharDest* Get() { return m_String; }

	const CharDest* operator*() const { return m_String; }
private:
	CharDest m_String[128];
};

using UnicodeToMultibyte = StringConverter<wchar_t, char>;
using MultibyteToUnicode = StringConverter<char, wchar_t>;
#define UNICODE_TO_MULTIBYTE(input) UnicodeToMultibyte(input).Get()
#define MULTIBYTE_TO_UNICODE(input) MultibyteToUnicode(input).Get()


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
std::string Sprintf(const char* pFormat, Args&&... args)
{
	int length = FormatString(nullptr, 0, pFormat, std::forward<Args&&>(args)...);
	std::string str;
	str.resize(length);
	FormatString(str.data(), length + 1, pFormat, std::forward<Args&&>(args)...);
	return str;
}

namespace CString
{
	void TrimSpaces(char* pStr);

	bool StrCmp(const char* pStrA, const char* pStrB, bool caseSensitive);

	constexpr inline char ToLower(char c)
	{
		return c >= 'A' && c <= 'Z' ? c - ('Z' - 'z') : c;
	}

	constexpr inline char ToUpper(char c)
	{
		return c >= 'a' && c <= 'z' ? c + ('Z' - 'z') : c;
	}

	constexpr inline void ToUpper(const char* pStr, char* pOut)
	{
		while (*pStr)
		{
			*pOut++ = (char)ToUpper(*pStr++);
		}
		*pOut = '\0';
	}

	constexpr inline void ToLower(const char* pStr, char* pOut)
	{
		while (*pStr)
		{
			*pOut++ = (char)ToLower(*pStr++);
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
	inline void ToString(uint32 val, std::string* pOut) { *pOut = Sprintf("%u", val); }
	inline void ToString(float val, std::string* pOut) { *pOut = Sprintf("%.3f", val); }
	inline void ToString(double val, std::string* pOut) { *pOut = Sprintf("%.3f", val); }
	inline void ToString(const char* val, std::string* pOut) { *pOut = val; }
	inline void ToString(bool val, std::string* pOut) { *pOut = Sprintf("%d", val ? "True" : "False"); }

	/// /////////////////////////////////////////////////////////////////////////

	namespace _Private
	{
		// INTERNAL: Create a tuple from string arguments
		template<size_t I, typename... Args>
		void TupleFromArguments(std::tuple<Args...>& t, const char** pArgs, int& failIndex)
		{
			if (failIndex == -1)
			{
				if (!CString::FromString(pArgs[I], std::get<I>(t)))
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
			_Private::TupleFromArguments<0>(pTuple, pArgs, *pFailIndex);
		}
		return pTuple;
	}
}
