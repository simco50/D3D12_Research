#include "stdafx.h"
#include "CString.h"

namespace CString
{
	void TrimSpaces(char* pStr)
	{
		char* pNewStart = pStr;
		while (*pNewStart == ' ') { ++pNewStart; }
		strcpy_s(pStr, INT_MAX, pNewStart); // Considered 'unsafe' but the destination won't even become bigger
		char* pEnd = pStr + strlen(pStr);
		while (pEnd > pStr && pEnd[-1] == ' ')
		{
			--pEnd;
		}
		*pEnd = '\0';
	}

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

	bool FromString(const char* pStr, char& out)
	{
		out = pStr[0];
		return true;
	}

	bool FromString(const char* pStr, int& out)
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

	bool FromString(const char* pStr, float& out)
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

	bool FromString(const char* pStr, double& out)
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

	bool FromString(const char* pStr, const char*& pOut)
	{
		pOut = pStr;
		return true;
	}

	bool FromString(const char* pStr, bool& out)
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
