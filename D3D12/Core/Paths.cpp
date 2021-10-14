#include "stdafx.h"
#include "Paths.h"

namespace Paths
{
	bool IsSlash(const char c)
	{
		if (c == '/')
			return true;
		return c == '/';
	}

	std::string GetFileName(const std::string& filePath)
	{
		auto it = std::find_if(filePath.rbegin(), filePath.rend(), [](const char c)
			{
				return IsSlash(c);
			});
		if (it == filePath.rend())
		{
			return filePath;
		}

		return filePath.substr(it.base() - filePath.begin());
	}

	std::string GetFileNameWithoutExtension(const std::string& filePath)
	{
		std::string fileName = GetFileName(filePath);
		size_t dotPos = fileName.find('.');
		if (dotPos == std::string::npos)
		{
			return fileName;
		}
		return fileName.substr(0, dotPos);
	}

	std::string GetFileExtenstion(const std::string& filePath)
	{
		size_t dotPos = filePath.rfind('.');
		if (dotPos == std::string::npos)
		{
			return "";
		}
		return filePath.substr(dotPos + 1);
	}

	std::string GetDirectoryPath(const std::string& filePath)
	{
		auto it = std::find_if(filePath.rbegin(), filePath.rend(), [](const char c)
			{
				return IsSlash(c);
			});
		if (it == filePath.rend())
		{
			if (filePath.rfind('.') == std::string::npos)
			{
				return "/";
			}
			return filePath;
		}

		return filePath.substr(0, it.base() - filePath.begin());
	}

	std::string Normalize(const std::string& filePath)
	{
		std::string output = std::string(filePath.begin(), filePath.end());
		NormalizeInline(output);
		return output;
	}

	void NormalizeInline(std::string& filePath)
	{
		for (char& c : filePath)
		{
			if (c == '\\')
			{
				c = '/';
			}
		}
		if (filePath.find("./") == 0)
		{
			filePath = std::string(filePath.begin() + 2, filePath.end());
		}
	}

	std::string ChangeExtension(const std::string& filePath, const std::string& newExtension)
	{
		size_t extensionStart = filePath.rfind('.');
		if (extensionStart == std::string::npos)
		{
			return filePath;
		}
		size_t lastSlash = filePath.rfind('/');
		if (extensionStart < lastSlash)
		{
			return filePath;
		}
		return filePath.substr(0, extensionStart + 1) + newExtension;
	}

	std::string MakeRelativePath(const std::string& basePath, const std::string& filePath)
	{
		size_t matchLength = 0;
		for (size_t i = 0; i < basePath.size(); i++)
		{
			if (basePath[i] != filePath[i])
			{
				break;
			}
			++matchLength;
		}
		return filePath.substr(matchLength);
	}

	void Combine(const std::vector<std::string>& elements, std::string& output)
	{
		// Reserve some conservative amount
		output.reserve(elements.size() * 20);
		for (size_t i = 0; i < elements.size(); i++)
		{
			output += elements[i];
			if (elements[i].back() != '/' && i != elements.size() - 1)
			{
				output += "/";
			}
		}
	}

	std::string Combine(const std::string& a, const std::string& b)
	{
		std::string output;
		Combine({ a, b }, output);
		return output;
	}

	bool FileExists(const std::string& filePath)
	{
		DWORD attributes = GetFileAttributesA(filePath.c_str());
		return (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY));
	}

	bool DirectoryExists(const std::string& filePath)
	{
		DWORD attributes = GetFileAttributesA(filePath.c_str());
		return (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY));
	}

	std::string GameDir()
	{
		return "./";
	}

	std::string SavedDir()
	{
		return GameDir() + "Saved/";
	}

	std::string ScreenshotDir()
	{
		return SavedDir() + "Screenshots/";
	}

	std::string LogsDir()
	{
		return SavedDir() + "Logs/";
	}

	std::string ProfilingDir()
	{
		return SavedDir() + "Profiling/";
	}

	std::string PakFilesDir()
	{
		return GameDir();
	}

	std::string ResourcesDir()
	{
		return GameDir() + "Resources/";
	}

	std::string ConfigDir()
	{
		return SavedDir() + "Config/";
	}

	std::string ShaderCacheDir()
	{
		return SavedDir() + "ShaderCache/";
	}

	std::string ShadersDir()
	{
		return ResourcesDir() + "Shaders/";
	}

	std::string GameIniFile()
	{
		return ConfigDir() + "Game.ini";
	}

	std::string EngineIniFile()
	{
		return ConfigDir() + "Engine.ini";
	}

	std::string WorkingDirectory()
	{
		char path[256];
		GetModuleFileNameA(nullptr, path, 256);
		return path;
	}

	bool CreateDirectoryTree(const std::string& path)
	{
		size_t slash = path.find('/', 0);
		while (slash != std::string::npos)
		{
			if (slash > 1)
			{
				std::string dirToCreate = path.substr(0, slash);
				const BOOL success = CreateDirectoryA(dirToCreate.c_str(), nullptr);
				DWORD error = GetLastError();
				if (success != S_OK && error == ERROR_PATH_NOT_FOUND)
				{
					return false;
				}
			}
			slash = path.find('/', slash + 1);
		}
		return true;
	}
}
