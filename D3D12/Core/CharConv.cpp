#include "stdafx.h"
#include "CharConv.h"

namespace CharConv
{
	bool StrCmp(const char* pStrA, const char* pStrB, bool caseSensitive)
	{
		while (*pStrA && *pStrB)
		{
			if (caseSensitive && *pStrA != *pStrB)
			{
				return false;
			}
			else if (!caseSensitive && std::tolower(*pStrA) != std::tolower(*pStrB))
			{
				return false;
			}
			++pStrA;
			++pStrB;
		}
		return *pStrA == *pStrB;
	}

	template<>
	bool StrConvert(const char* pStr, char& out)
	{
		out = pStr[0];
		return true;
	}

	template<>
	bool StrConvert(const char* pStr, int& out)
	{
		size_t idx = 0;
		char sign = 1;
		out = 0;
		while (*pStr != '\0')
		{
			if (idx == 0 && *pStr == '-')
			{
				sign = -1;
			}
			else if (*pStr && *pStr >= '0' && *pStr <= '9')
			{
				out *= 10;
				out += *pStr - '0';
			}
			else
			{
				return false;
			}

			++pStr;
			++idx;
		}
		out *= sign;
		return true;
	}

	template<> bool StrConvert(const char* pStr, float& out)
	{
		size_t idx = 0;
		char sign = 1;
		char comma = 0;
		int divisor = 1;
		out = 0.0f;
		while (*pStr != '\0')
		{
			if (idx == 0 && *pStr == '-')
			{
				sign = -1;
			}
			else if (*pStr == '.' && comma == 0)
			{
				comma = 1;
			}
			else if (*pStr && *pStr >= '0' && *pStr <= '9')
			{
				out *= 10;
				out += *pStr - '0';
				if (comma)
				{
					divisor *= 10;
				}
			}
			else if (*pStr == 'f' && pStr[1] == '\0')
			{

			}
			else
			{
				return false;
			}

			++pStr;
			++idx;
		}
		out *= sign;
		out /= divisor;
		return true;
	}

	template<>
	bool StrConvert(const char* pStr, double& out)
	{
		size_t idx = 0;
		char sign = 1;
		char comma = 0;
		int divisor = 1;
		out = 0.0;
		while (*pStr != '\0')
		{
			if (idx == 0 && *pStr == '-')
			{
				sign = -1;
			}
			else if (*pStr == '.' && comma == 0)
			{
				comma = 1;
			}
			else if (*pStr && *pStr >= '0' && *pStr <= '9')
			{
				out *= 10;
				out += *pStr - '0';
				if (comma)
				{
					divisor *= 10;
				}
			}
			else
			{
				return false;
			}

			++pStr;
			++idx;
		}
		out *= sign;
		out /= divisor;
		return true;
	}

	template<>
	bool StrConvert(const char* pStr, const char*& pOut)
	{
		pOut = pStr;
		return true;
	}

	template<>
	bool StrConvert(const char* pStr, bool& out)
	{
		if (*pStr == '0' || StrCmp(pStr, "false", false))
		{
			out = false;
			return true;
		}
		else if (*pStr == '1' || StrCmp(pStr, "true", false))
		{
			out = true;
			return true;
		}
		return false;
	}
}
