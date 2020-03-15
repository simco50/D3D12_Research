#pragma once

class CommandLine
{
public:
	static bool Parse(const char* pCommandLine);

	static bool GetBool(const std::string& parameter);
	static const std::string& Get() { return m_CommandLine; }

private:
	static std::unordered_map<std::string, std::string> m_Parameters;
	static std::string m_CommandLine;
};