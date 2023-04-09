#pragma once

#define E_LOG(level, message, ...) Console::LogFormat(LogType::level, message, __VA_ARGS__)

enum class LogType
{
	VeryVerbose,
	Verbose,
	Info,
	Warning,
	Error,
	FatalError,
};

class Console
{
public:
	struct LogEntry
	{
		static const uint32 MAX_LOG_LENGTH = 128;

		LogEntry(const char* pMsg, const LogType type)
			: Type(type)
		{
			strcpy_s(pMessage, pMsg);
		}
		char pMessage[MAX_LOG_LENGTH];
		LogType Type;
	};
	static void Initialize();
	static void Shutdown();
	static void Log(const char* message, LogType type = LogType::Info);
	static void LogFormat(LogType type, const char* format, ...);
	static void SetVerbosity(LogType type);

	static const std::deque<LogEntry>& GetHistory();

private:
	static void FlushLog(const LogEntry& entry);
};
