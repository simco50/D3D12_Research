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
	Console();
	~Console();

	struct LogEntry
	{
		LogEntry(const std::string& message, const LogType type)
			: Message(message), Type(type)
		{}
		std::string Message;
		LogType Type;
	};
	static void Startup();
	static bool LogHRESULT(const std::string &source, HRESULT hr);
	static bool LogHRESULT(char* source, HRESULT hr);
	static void Log(const std::string &message, LogType type = LogType::Info);
	static void LogFormat(LogType type, const char* format, ...);
	static void LogFormat(LogType type, const std::string& format, ...);
	static void SetVerbosity(LogType type);

	static const std::deque<LogEntry>& GetHistory();

private:
	void InitializeConsoleWindow();

	char* m_ConvertBuffer;
	const size_t m_ConvertBufferSize = 4096;
	std::queue<LogEntry> m_MessageQueue;
	std::mutex m_QueueMutex;

	LogType m_Verbosity = LogType::Info;
	HANDLE m_ConsoleHandle = nullptr;

	std::deque<LogEntry> m_History;
};
