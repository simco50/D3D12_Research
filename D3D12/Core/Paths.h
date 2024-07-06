#pragma once

namespace Paths
{
	namespace Private
	{
		inline const char* GetCharPtr(const char* pStr) { return pStr; }
		inline const char* GetCharPtr(const String& str) { return str.c_str(); }
	}

	bool IsSlash(const char c);

	String GetFileName(const String& filePath);
	String GetFileNameWithoutExtension(const String& filePath);
	String GetFileExtenstion(const String& filePath);
	String GetDirectoryPath(const String& filePath);

	String Normalize(const String& filePath);
	void NormalizeInline(String& filePath);
	bool ResolveRelativePaths(String& path);

	String ChangeExtension(const String& filePath, const String& newExtension);

	String MakeAbsolute(const char* pFilePath);
	String MakeRelativePath(const String& basePath, const String& filePath);

	void CombineInner(const char** pElements, uint32 numElements, String& output);

	template<typename ...Args>
	String Combine(Args&&... args)
	{
		const char* paths[] = { Private::GetCharPtr(std::forward<Args>(args))... };
		String result;
		CombineInner(paths, ARRAYSIZE(paths), result);
		return result;
	}

	bool FileExists(const char* pFilePath);
	bool DirectoryExists(const char* pFilePath);

	String GameDir();

	String SavedDir();

	String ScreenshotDir();
	String LogsDir();
	String ProfilingDir();
	String PakFilesDir();
	String ResourcesDir();
	String ConfigDir();
	String ShaderCacheDir();
	String ShadersDir();

	String GameIniFile();
	String EngineIniFile();

	String WorkingDirectory();

	void GetFileTime(const char* pFilePath, uint64& creationTime, uint64& lastAccessTime, uint64& modificationTime);

	bool CreateDirectoryTree(const String& path);
};
