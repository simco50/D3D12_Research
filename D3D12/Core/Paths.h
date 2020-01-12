#pragma once

struct Paths
{
	static bool IsSlash(const char c);

	static std::string GetFileName(const std::string& filePath);
	static std::string GetFileNameWithoutExtension(const std::string& filePath);
	static std::string GetFileExtenstion(const std::string& filePath);
	static std::string GetDirectoryPath(const std::string& filePath);

	static std::string Normalize(const std::string& filePath);
	static void NormalizeInline(std::string& filePath);

	static std::string ChangeExtension(const std::string& filePath, const std::string& newExtension);

	static std::string MakeRelativePath(const std::string& basePath, const std::string& filePath);

	static std::string Combine(const std::string& a, const std::string& b);
	static void Combine(const std::vector<std::string>& elements, std::string& output);

	static bool FileExists(const std::string& filePath);
	static bool DirectoryExists(const std::string& filePath);

	static std::string GameDir();

	static std::string SavedDir();

	static std::string ScreenshotDir();
	static std::string LogsDir();
	static std::string ProfilingDir();
	static std::string PakFilesDir();
	static std::string ResourcesDir();
	static std::string ConfigDir();
	static std::string ShaderCacheDir();

	static std::string GameIniFile();
	static std::string EngineIniFile();

	static std::string WorkingDirectory();
};