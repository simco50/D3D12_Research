#include "stdafx.h"
#include "Assert.h"

bool ReportAssert(const char* pFilePath, int line, const char* pExpression, const char* pMessage)
{
	Console::Log("########################## Assert Failed ##########################", LogType::Warning);
	Console::LogFormat(LogType::Warning, "# File: %s (%d)", pFilePath, line);
	Console::LogFormat(LogType::Warning, "# Expression: %s", pExpression);
	Console::LogFormat(LogType::Warning, "# Message: %s", pMessage);
	Console::Log("###################################################################", LogType::Warning);
	return true;
}
