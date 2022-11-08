#pragma once

namespace Paths
{
	namespace Private
	{
		inline const char* GetCharPtr(const char* pStr) { return pStr; }
		inline const char* GetCharPtr(const std::string& str) { return str.c_str(); }
	}

	bool IsSlash(const char c);

	std::string GetFileName(const std::string& filePath);
	std::string GetFileNameWithoutExtension(const std::string& filePath);
	std::string GetFileExtenstion(const std::string& filePath);
	std::string GetDirectoryPath(const std::string& filePath);

	std::string Normalize(const std::string& filePath);
	void NormalizeInline(std::string& filePath);
	bool ResolveRelativePaths(std::string& path);

	std::string ChangeExtension(const std::string& filePath, const std::string& newExtension);

	std::string MakeRelativePath(const std::string& basePath, const std::string& filePath);

	void CombineInner(const char** pElements, uint32 numElements, std::string& output);

	template<typename ...Args>
	std::string Combine(Args&&... args)
	{
		const char* paths[] = { Private::GetCharPtr(std::forward<Args>(args))... };
		std::string result;
		CombineInner(paths, ARRAYSIZE(paths), result);
		return result;
	}

	bool FileExists(const char* pFilePath);
	bool DirectoryExists(const char* pFilePath);

	std::string GameDir();

	std::string SavedDir();

	std::string ScreenshotDir();
	std::string LogsDir();
	std::string ProfilingDir();
	std::string PakFilesDir();
	std::string ResourcesDir();
	std::string ConfigDir();
	std::string ShaderCacheDir();
	std::string ShadersDir();

	std::string GameIniFile();
	std::string EngineIniFile();

	std::string WorkingDirectory();

	bool CreateDirectoryTree(const std::string& path);
};
