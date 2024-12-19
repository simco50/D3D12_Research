#pragma once

#include "CString.h"

#define ENABLE_ASSERTS 1

#if ENABLE_ASSERTS

bool ReportAssert(const char* pFilePath, int line, const char* pExpression, const char* pMessage);

template<typename... Args>
bool ReportAssertFmt(const char* pFilePath, int line, const char* pExpression, const char* pMessage = nullptr, Args&&... args)
{
	char message[4096];
	if(pMessage)
		FormatString(message, ARRAYSIZE(message), pMessage, args...);	
	return ReportAssert(pFilePath, line, pExpression, pMessage ? message : "");
}

#define gAssert(expression, ...)																\
	do																							\
	{																							\
		if(!(expression) && ReportAssertFmt(__FILE__, __LINE__, #expression, __VA_ARGS__))		\
			__debugbreak();																		\
	} while(0)

#define gAssertOnce(expression, ...)															\
	do																							\
	{																							\
		if(!(expression))																		\
		{																						\
			static bool has_executed = false;													\
			if(!has_executed)																	\
			{																					\
				has_executed = true;															\
				if(ReportAssertFmt(__FILE__, __LINE__, #expression, __VA_ARGS__))				\
					__debugbreak();																\
			}																					\
		}																						\
	} while(0)


#define gVerify(expression, validation, ...)													\
	do																							\
	{																							\
		if(!((expression) validation))															\
		{																						\
			Console::LogFormat(LogType::Warning, __VA_ARGS__);									\
			__debugbreak();																		\
		}																						\
	} while(0)

#else

#define gAssert(expression, ...) do {} while(0);
#define gVerify(expression, validation, ...) (expression)
#define gAssertOnce(expression, ...) do {} while(0);

#endif

#define gBoundCheck(x, min, max, ...) gAssert(x >= min && x < max, __VA_ARGS__)

#define gUnreachable() gAssert(false, "Should not have reached this point!")
