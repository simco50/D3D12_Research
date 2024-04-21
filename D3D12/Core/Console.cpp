#include "stdafx.h"
#include "Console.h"
#include "CommandLine.h"

namespace Win32Console
{
	static HANDLE Open()
	{
		HANDLE handle = NULL;
		if (AllocConsole())
		{
			// Redirect the CRT standard input, output, and error handles to the console
			FILE* pCout;
			freopen_s(&pCout, "CONIN$", "r", stdin);
			freopen_s(&pCout, "CONOUT$", "w", stdout);
			freopen_s(&pCout, "CONOUT$", "w", stderr);

			handle = GetStdHandle(STD_OUTPUT_HANDLE);

			//Disable Close-Button
			HWND hwnd = GetConsoleWindow();
			if (hwnd != nullptr)
			{
				HMENU hMenu = GetSystemMenu(hwnd, FALSE);
				if (hMenu != nullptr)
				{
					DeleteMenu(hMenu, SC_CLOSE, MF_BYCOMMAND);
				}
			}
		}
		return handle;;
	}

	static void Close(HANDLE handle)
	{
		if(handle)
			CloseHandle(handle);
	}
};

static HANDLE sConsoleHandle = nullptr;
static std::mutex sLogMutex;
static LogType sVerbosity;
static std::deque<Console::LogEntry> sHistory;
std::array<char, 8192> Console::sConvertBuffer;

void Console::Initialize()
{
	if (CommandLine::GetBool("noconsole") == false)
	{
		sConsoleHandle = Win32Console::Open();
	}
	E_LOG(Info, "Startup");
}

void Console::Shutdown()
{
	Win32Console::Close(sConsoleHandle);
}

void Console::Log(const char* message, LogType type)
{
	if ((int)type < (int)sVerbosity)
		return;

	std::scoped_lock lock(sLogMutex);

	const char* pVerbosityMessage = "";
	switch (type)
	{
	case LogType::Info:
		if (sConsoleHandle)
			SetConsoleTextAttribute(sConsoleHandle, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
		pVerbosityMessage = "[INFO]";
		break;
	case LogType::Warning:
		if (sConsoleHandle)
			SetConsoleTextAttribute(sConsoleHandle, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
		pVerbosityMessage = "[WARNING]";
		break;
	case LogType::Error:
	case LogType::FatalError:
		if (sConsoleHandle)
			SetConsoleTextAttribute(sConsoleHandle, FOREGROUND_RED | FOREGROUND_INTENSITY);
		pVerbosityMessage = "[ERROR]";
		break;
	default:
		break;
	}

	char messageBuffer[4096];
	FormatString(messageBuffer, ARRAYSIZE(messageBuffer), "%s %s\n", pVerbosityMessage, message);
	printf("%s %s\n", pVerbosityMessage, message);
	OutputDebugStringA(messageBuffer);

	sHistory.push_back({ message, type });
	while (sHistory.size() > 50)
		sHistory.pop_front();

	if (sConsoleHandle)
		SetConsoleTextAttribute(sConsoleHandle, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);

	if (type == LogType::FatalError)
		abort();
}

void Console::SetVerbosity(LogType type)
{
	sVerbosity = type;
}

const std::deque<Console::LogEntry>& Console::GetHistory()
{
	return sHistory;
}

void Console::FlushLog(const LogEntry& log)
{
	const char* pVerbosityMessage = "";
	switch (log.Type)
	{
	case LogType::Info:
		if (sConsoleHandle)
			SetConsoleTextAttribute(sConsoleHandle, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
		pVerbosityMessage = "[INFO]";
		break;
	case LogType::Warning:
		if (sConsoleHandle)
			SetConsoleTextAttribute(sConsoleHandle, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
		pVerbosityMessage = "[WARNING]";
		break;
	case LogType::Error:
	case LogType::FatalError:
		if (sConsoleHandle)
			SetConsoleTextAttribute(sConsoleHandle, FOREGROUND_RED | FOREGROUND_INTENSITY);
		pVerbosityMessage = "[ERROR]";
		break;
	default:
		break;
	}

	char messageBuffer[4096];
	FormatString(messageBuffer, ARRAYSIZE(messageBuffer), "%s %s\n", pVerbosityMessage, log.Message.c_str());
	printf("%s %s\n", pVerbosityMessage, log.Message.c_str());
	OutputDebugStringA(messageBuffer);

	sHistory.push_back(log);
	while (sHistory.size() > 50)
	{
		sHistory.pop_front();
	}

	if (sConsoleHandle)
	{
		SetConsoleTextAttribute(sConsoleHandle, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
	}
}
