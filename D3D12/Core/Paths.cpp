#include "stdafx.h"
#include "Paths.h"

namespace Paths
{
	bool IsSlash(const char c)
	{
		return c == '\\' || c == '/';
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
		std::string fileName = GetFileName(filePath);
		return filePath.substr(0, filePath.length() - fileName.length());
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

	bool ResolveRelativePaths(std::string& path)
	{
		for (;;)
		{
			size_t index = path.rfind("../");
			if (index == std::string::npos)
				break;
			size_t idx0 = path.rfind('/', index);
			if (idx0 == std::string::npos)
				return false;
			idx0 = path.rfind('/', idx0 - 1);
			if (idx0 != std::string::npos)
				path = path.substr(0, idx0 + 1) + path.substr(index + 3);
		}
		return true;
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

	void CombineInner(const char** pElements, uint32 numElements, std::string& output)
	{
		size_t stringLength = 0;
		for (size_t i = 0; i < numElements; i++)
		{
			stringLength += strlen(pElements[i]);
		}
		output.reserve(stringLength);
		for (size_t i = 0; i < numElements; i++)
		{
			if (strlen(pElements[i]) > 0)
			{
				output += pElements[i];
				if (output.back() != '/' && i != numElements - 1)
				{
					output += "/";
				}
			}
		}
	}

	bool FileExists(const char* pFilePath)
	{
		DWORD attributes = GetFileAttributesA(pFilePath);
		return (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY));
	}

	bool DirectoryExists(const char* pFilePath)
	{
		DWORD attributes = GetFileAttributesA(pFilePath);
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

	void GetFileTime(const char* pFilePath, uint64& creationTime, uint64& lastAccessTime, uint64& modificationTime)
	{
		HANDLE file = ::CreateFileA(pFilePath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
		FILETIME cTime, aTime, mTime;
		::GetFileTime(file, &cTime, &aTime, &mTime);
		creationTime = (uint64)(LARGE_INTEGER{ cTime.dwLowDateTime, (long)cTime.dwHighDateTime }.QuadPart * 1e-7);
		lastAccessTime = (uint64)(LARGE_INTEGER{ aTime.dwLowDateTime, (long)aTime.dwHighDateTime }.QuadPart * 1e-7);
		modificationTime = (uint64)(LARGE_INTEGER{ mTime.dwLowDateTime, (long)mTime.dwHighDateTime }.QuadPart * 1e-7);
		CloseHandle(file);
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
				if (success != TRUE && error == ERROR_PATH_NOT_FOUND)
				{
					return false;
				}
			}
			slash = path.find('/', slash + 1);
		}
		return true;
	}
}
