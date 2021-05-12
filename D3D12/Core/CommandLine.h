#pragma once

class CommandLine
{
public:
	static bool Parse(const char* pCommandLine);

	static bool GetInt(const char* name, int& value, int defaultValue = 0);
	static bool GetBool(const char* parameter);
	static const std::string& Get();
};
