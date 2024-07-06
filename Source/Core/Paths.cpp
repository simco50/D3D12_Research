#include "stdafx.h"
#include "Paths.h"

namespace Paths
{
	bool IsSlash(const char c)
	{
		return c == '\\' || c == '/';
	}

	String GetFileName(const String& filePath)
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

	String GetFileNameWithoutExtension(const String& filePath)
	{
		String fileName = GetFileName(filePath);
		size_t dotPos = fileName.find('.');
		if (dotPos == String::npos)
		{
			return fileName;
		}
		return fileName.substr(0, dotPos);
	}

	String GetFileExtenstion(const String& filePath)
	{
		size_t dotPos = filePath.rfind('.');
		if (dotPos == String::npos)
		{
			return "";
		}
		return filePath.substr(dotPos + 1);
	}

	String GetDirectoryPath(const String& filePath)
	{
		String fileName = GetFileName(filePath);
		return filePath.substr(0, filePath.length() - fileName.length());
	}

	String Normalize(const String& filePath)
	{
		String output = String(filePath.begin(), filePath.end());
		NormalizeInline(output);
		return output;
	}

	void NormalizeInline(String& filePath)
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
			filePath = String(filePath.begin() + 2, filePath.end());
		}
	}

	bool ResolveRelativePaths(String& path)
	{
		for (;;)
		{
			size_t index = path.rfind("../");
			if (index == String::npos)
				break;
			size_t idx0 = path.rfind('/', index);
			if (idx0 == String::npos)
				return false;
			idx0 = path.rfind('/', idx0 - 1);
			if (idx0 != String::npos)
				path = path.substr(0, idx0 + 1) + path.substr(index + 3);
		}
		return true;
	}

	String ChangeExtension(const String& filePath, const String& newExtension)
	{
		size_t extensionStart = filePath.rfind('.');
		if (extensionStart == String::npos)
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

	String MakeAbsolute(const char* pFilePath)
	{
		char fullPath[MAX_PATH];
		GetFullPathNameA(pFilePath, MAX_PATH, fullPath, nullptr);
		return fullPath;
	}


	String MakeRelativePath(const String& basePath, const String& filePath)
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

	void CombineInner(const char** pElements, uint32 numElements, String& output)
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

	String GameDir()
	{
		return "./";
	}

	String SavedDir()
	{
		return GameDir() + "Saved/";
	}

	String ScreenshotDir()
	{
		return SavedDir() + "Screenshots/";
	}

	String LogsDir()
	{
		return SavedDir() + "Logs/";
	}

	String ProfilingDir()
	{
		return SavedDir() + "Profiling/";
	}

	String PakFilesDir()
	{
		return GameDir();
	}

	String ResourcesDir()
	{
		return GameDir() + "Resources/";
	}

	String ConfigDir()
	{
		return SavedDir() + "Config/";
	}

	String ShaderCacheDir()
	{
		return SavedDir() + "ShaderCache/";
	}

	String ShadersDir()
	{
		return ResourcesDir() + "Shaders/";
	}

	String GameIniFile()
	{
		return ConfigDir() + "Game.ini";
	}

	String EngineIniFile()
	{
		return ConfigDir() + "Engine.ini";
	}

	String WorkingDirectory()
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

	bool CreateDirectoryTree(const String& path)
	{
		size_t slash = path.find('/', 0);
		while (slash != String::npos)
		{
			if (slash > 1)
			{
				String dirToCreate = path.substr(0, slash);
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
