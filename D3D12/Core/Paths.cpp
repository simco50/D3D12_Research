#include "stdafx.h"
#include "Paths.h"

bool Paths::IsSlash(const char c)
{
	if (c == '/')
		return true;
	return c == '/';
}

std::string Paths::GetFileName(const std::string& filePath)
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

std::string Paths::GetFileNameWithoutExtension(const std::string& filePath)
{
	std::string fileName = GetFileName(filePath);
	size_t dotPos = fileName.find('.');
	if (dotPos == std::string::npos)
	{
		return fileName;
	}
	return fileName.substr(0, dotPos);
}

std::string Paths::GetFileExtenstion(const std::string& filePath)
{
	size_t dotPos = filePath.rfind('.');
	if (dotPos == std::string::npos)
	{
		return "";
	}
	return filePath.substr(dotPos + 1);
}

std::string Paths::GetDirectoryPath(const std::string& filePath)
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

std::string Paths::Normalize(const std::string& filePath)
{
	std::string output = std::string(filePath.begin(), filePath.end());
	NormalizeInline(output);
	return output;
}

void Paths::NormalizeInline(std::string& filePath)
{
	for (char& c : filePath)
	{
		if (c == '/')
		{
			c = '/';
		}
		c = (char)tolower(c);
	}
	if (filePath.find("./") == 0)
	{
		filePath = std::string(filePath.begin() + 2, filePath.end());
	}
}

std::string Paths::ChangeExtension(const std::string& filePath, const std::string& newExtension)
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

std::string Paths::MakeRelativePath(const std::string& basePath, const std::string& filePath)
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

void Paths::Combine(const std::vector<std::string>& elements, std::string& output)
{
	std::stringstream stream;
	for (size_t i = 0; i < elements.size() ; i++)
	{
		stream << elements[i];
		if (i != elements.size() - 1)
		{
			stream << "/";
		}
	}
	output = stream.str();
}

std::string Paths::Combine(const std::string& a, const std::string& b)
{
	std::string output;
	Combine({a, b}, output);
	return output;
}

bool Paths::FileExists(const std::string& filePath)
{
#ifdef PLATFORM_WINDOWS
	DWORD attributes = GetFileAttributes(filePath.c_str());
	return (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY));
#else
	return false;
#endif
}

bool Paths::DirectoryExists(const std::string& filePath)
{
#ifdef PLATFORM_WINDOWS
	DWORD attributes = GetFileAttributes(filePath.c_str());
	return (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY));
#else
	return false;
#endif
}

std::string Paths::GameDir()
{
	return "./";
}

std::string Paths::SavedDir()
{
	return GameDir() + "Saved/";
}

std::string Paths::ScreenshotDir()
{
	return SavedDir() + "Screenshots/";
}

std::string Paths::LogsDir()
{
	return SavedDir() + "Logs/";
}

std::string Paths::ProfilingDir()
{
	return SavedDir() + "Profiling/";
}

std::string Paths::PakFilesDir()
{
	return GameDir();
}

std::string Paths::ResourcesDir()
{
	return GameDir() + "Resources/";
}

std::string Paths::ConfigDir()
{
	return SavedDir() + "Config/";
}

std::string Paths::ShaderCacheDir()
{
	return SavedDir() + "ShaderCache/";
}

std::string Paths::ShadersDir()
{
	return ResourcesDir() + "Shaders/";
}

std::string Paths::GameIniFile()
{
	return ConfigDir() + "Game.ini";
}

std::string Paths::EngineIniFile()
{
	return ConfigDir() + "Engine.ini";
}

std::string Paths::WorkingDirectory()
{
	char path[256];
	GetModuleFileName(nullptr, path, 256);
	return path;
}

bool Paths::CreateDirectoryTree(const std::string& path)
{
	size_t slash = path.find('/', 0);
	while (slash != std::string::npos)
	{
		if (slash > 1)
		{
			std::string dirToCreate = path.substr(0, slash);
			const BOOL success = CreateDirectory(dirToCreate.c_str(), nullptr);
			if (!success)
			{
				return false;
			}
		}
		slash = path.find('/', slash + 1);
	}
	return true;
}