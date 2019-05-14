#include "stdafx.h"
#include "Console.h"

static Console* consoleInstance = nullptr;

Console::Console()
{
}

Console::~Console()
{
	delete[] m_ConvertBuffer;
}

void Console::Startup()
{
	static Console console;
	consoleInstance = &console;

#ifdef _DEBUG
	console.InitializeConsoleWindow();
#endif

	console.m_ConvertBuffer = new char[console.m_ConvertBufferSize];
}

bool Console::LogHRESULT(const std::string &source, HRESULT hr)
{
	if (FAILED(hr))
	{
		if (FACILITY_WINDOWS == HRESULT_FACILITY(hr))
		{
			hr = HRESULT_CODE(hr);
		}

		std::stringstream ss;
		if (source.size() != 0)
		{
			ss << "Source: ";
			ss << source;
			ss << "\n";
		}
		ss << "Message: ";

		char* errorMsg;
		if (FormatMessage(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
			nullptr, hr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPSTR)&errorMsg, 0, nullptr) != 0)
		{
			OutputDebugString(errorMsg);
			ss << errorMsg;
		}

		Log(ss.str(), LogType::Error);
		return true;
	}

	return false;
}

bool Console::LogHRESULT(char* source, HRESULT hr)
{
	return LogHRESULT(std::string(source), hr);
}

void Console::Log(const std::string &message, LogType type)
{
	if ((int)type < (int)consoleInstance->m_Verbosity)
	{
		return;
	}

	std::stringstream stream;
	switch (type)
	{
	case LogType::Info:
		stream << "[INFO] ";
		break;
	case LogType::Warning:
		if (consoleInstance->m_ConsoleHandle)
			SetConsoleTextAttribute(consoleInstance->m_ConsoleHandle, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
		stream << "[WARNING] ";
		break;
	case LogType::Error:
	case LogType::FatalError:
		if (consoleInstance->m_ConsoleHandle)
			SetConsoleTextAttribute(consoleInstance->m_ConsoleHandle, FOREGROUND_RED | FOREGROUND_INTENSITY);
		stream << "[ERROR] ";
		break;
	default:
		break;
	}

	stream << message;
	const std::string output = stream.str();
	std::cout << output << std::endl;
	if (consoleInstance->m_ConsoleHandle)
	{
		SetConsoleTextAttribute(consoleInstance->m_ConsoleHandle, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
	}

	consoleInstance->m_History.push_back(LogEntry(message, type));
	if (consoleInstance->m_History.size() > 50)
	{
		consoleInstance->m_History.pop_front();
	}

	if (type == LogType::Error)
	{
		__debugbreak();
	}
	else if (type == LogType::FatalError)
	{
		abort();
	}
}

void Console::LogFormat(LogType type, const char* format, ...)
{
	va_list ap;

	va_start(ap, format);
	_vsnprintf_s(&consoleInstance->m_ConvertBuffer[0], consoleInstance->m_ConvertBufferSize, consoleInstance->m_ConvertBufferSize, format, ap);
	va_end(ap);
	Log(&consoleInstance->m_ConvertBuffer[0], type);
}

void Console::LogFormat(LogType type, const std::string& format, ...)
{
	va_list ap;

	const char* f = format.c_str();
	va_start(ap, f);
	_vsnprintf_s(&consoleInstance->m_ConvertBuffer[0], consoleInstance->m_ConvertBufferSize, consoleInstance->m_ConvertBufferSize, f, ap);
	va_end(ap);
	Log(&consoleInstance->m_ConvertBuffer[0], type);
}

void Console::SetVerbosity(LogType type)
{
	consoleInstance->m_Verbosity = type;
}

const std::deque<Console::LogEntry>& Console::GetHistory()
{
	return consoleInstance->m_History;
}

void Console::InitializeConsoleWindow()
{
	if (AllocConsole())
	{
		// Redirect the CRT standard input, output, and error handles to the console
		FILE* pCout;
		freopen_s(&pCout, "CONIN$", "r", stdin);
		freopen_s(&pCout, "CONOUT$", "w", stdout);
		freopen_s(&pCout, "CONOUT$", "w", stderr);

		//Clear the error state for each of the C++ standard stream objects. We need to do this, as
		//attempts to access the standard streams before they refer to a valid target will cause the
		//iostream objects to enter an error state. In versions of Visual Studio after 2005, this seems
		//to always occur during startup regardless of whether anything has been read from or written to
		//the console or not.
		std::wcout.clear();
		std::cout.clear();
		std::wcerr.clear();
		std::cerr.clear();
		std::wcin.clear();
		std::cin.clear();

		//Set ConsoleHandle
		m_ConsoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);

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
}