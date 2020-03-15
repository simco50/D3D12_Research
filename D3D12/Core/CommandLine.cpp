#include "stdafx.h"
#include "CommandLine.h"

std::unordered_map<std::string, std::string> CommandLine::m_Parameters;
std::string CommandLine::m_CommandLine;

bool CommandLine::Parse(const char* pCommandLine)
{
	m_CommandLine = pCommandLine;
	m_Parameters.clear();
	bool quoted = false;
	bool skipArgument = false;

	int commandStart = 0;
	bool hasValue = false;
	std::string identifier;

	for (size_t i = 0; i < m_CommandLine.size(); i++)
	{
		if (m_CommandLine[i] == '\"')
		{
			quoted = !quoted;
		}
		else if (m_CommandLine[i] == '-' && !quoted)
		{
			commandStart = (int)i + 1;
		}
		else if (m_CommandLine[i] == '=' && !quoted)
		{
			identifier = m_CommandLine.substr(commandStart, i - commandStart);
			commandStart = (int)i + 1;
			hasValue = true;
		}
		else if (m_CommandLine[i] == ' ' && !quoted)
		{
			if (hasValue)
			{
				std::string value = m_CommandLine.substr(commandStart, i - commandStart);
				if (value.front() == '\"' && value.back() == '\"')
				{
					value = value.substr(1, value.length() - 2);
				}

				m_Parameters[identifier] = value;
			}
			else
			{
				m_Parameters[m_CommandLine.substr(commandStart, i - commandStart)] = "1";
			}
			commandStart = -1;
		}
	}

	if (commandStart > -1)
	{
		if (hasValue)
		{
			std::string value = m_CommandLine.substr(commandStart);
			if (value.front() == '\"' && value.back() == '\"')
			{
				value = value.substr(1, value.length() - 2);
			}

			m_Parameters[identifier] = value;
		}
		else
		{
			m_Parameters[m_CommandLine.substr(commandStart)] = "1";
		}
		commandStart = -1;
	}

	return true;
}

bool CommandLine::GetBool(const std::string& parameter)
{
	return m_Parameters.find(parameter) != m_Parameters.end();
}