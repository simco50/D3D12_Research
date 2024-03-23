#include "stdafx.h"
#include "Shader.h"
#include "Core/Paths.h"
#include "Core/CommandLine.h"
#include "Core/FileWatcher.h"
#include "dxc/dxcapi.h"
#include "dxc/d3d12shader.h"
#include "D3D.h"
#include "Core/Stream.h"
#include "Core/Profiler.h"

namespace ShaderCompiler
{
	constexpr const char* pCompilerPath = "dxcompiler.dll";
	constexpr const char* pShaderSymbolsPath = "Saved/ShaderSymbols/";

	static Ref<IDxcUtils> pUtils;
	static Ref<IDxcCompiler3> pCompiler3;
	static Ref<IDxcValidator> pValidator;
	static Ref<IDxcIncludeHandler> pDefaultIncludeHandler;
	static std::mutex IncludeCacheMutex;

	struct CachedFile
	{
		Ref<IDxcBlobEncoding> pBlob;
		uint64 Timestamp;
	};
	static std::unordered_map<StringHash, CachedFile> IncludeCache;
	std::mutex ShaderCacheMutex;

	struct CompileJob
	{
		std::string FilePath;
		std::string EntryPoint;
		std::string Target;
		Span<ShaderDefine> Defines;
		std::vector<std::string> IncludeDirs;
		uint8 MajVersion;
		uint8 MinVersion;
		bool EnableDebugMode;
	};

	struct CompileResult
	{
		static constexpr int Version = 7;

		std::string ErrorMessage;
		ShaderBlob pBlob;
		Ref<IUnknown> pReflection;
		std::vector<std::string> Includes;
		uint64 ShaderHash[2];
		bool IsDebug;

		bool Success() const { return pBlob.Get() && ErrorMessage.length() == 0; }
	};

	constexpr const char* GetShaderTarget(ShaderType type)
	{
		switch (type)
		{
		case ShaderType::Vertex:		return "vs";
		case ShaderType::Pixel:			return "ps";
		case ShaderType::Compute:		return "cs";
		case ShaderType::Mesh:			return "ms";
		case ShaderType::Amplification: return "as";
		default:						return "lib";
		}
	}

	static void LoadDXC()
	{
		FN_PROC(DxcCreateInstance);

		HMODULE lib = LoadLibraryA(pCompilerPath);
		DxcCreateInstanceFn.Load(lib);

		VERIFY_HR(DxcCreateInstanceFn(CLSID_DxcUtils, IID_PPV_ARGS(pUtils.GetAddressOf())));
		VERIFY_HR(DxcCreateInstanceFn(CLSID_DxcCompiler, IID_PPV_ARGS(pCompiler3.GetAddressOf())));
		VERIFY_HR(DxcCreateInstanceFn(CLSID_DxcValidator, IID_PPV_ARGS(pValidator.GetAddressOf())));
		VERIFY_HR(pUtils->CreateDefaultIncludeHandler(pDefaultIncludeHandler.GetAddressOf()));
		E_LOG(Info, "Loaded %s", pCompilerPath);
	}

	static bool ResolveFilePath(const CompileJob& job, std::string& outPath)
	{
		for (const std::string& includeDir : job.IncludeDirs)
		{
			outPath = Paths::Combine(includeDir, job.FilePath);
			if (Paths::FileExists(outPath.c_str()))
				return true;
		}
		outPath = "";
		return false;
	}

	static bool TryLoadFromCache(const char* pCachePath, const CompileJob& compileJob, CompileResult& result)
	{
		std::lock_guard lock(ShaderCacheMutex);

		// See if the cache file exists
		if (!Paths::FileExists(pCachePath))
			return false;

		std::string shaderFullPath;
		if (!ResolveFilePath(compileJob, shaderFullPath))
			return false;

		// Check if the shader source is not newer than the cached file
		uint64 cacheTime, temp;
		Paths::GetFileTime(pCachePath, temp, temp, cacheTime);

		auto TestFileTime = [&](const char* pFilePath) {
			uint64 shaderTime;
			Paths::GetFileTime(pFilePath, temp, temp, shaderTime);
			return cacheTime >= shaderTime;
		};

		if (!TestFileTime(shaderFullPath.c_str()))
			return false;

		FileStream fs;
		fs.Open(pCachePath, FileMode::Read);
		uint32 version = 0;
		fs >> version;
		if (version != CompileResult::Version)
			return false;

		fs >> result.ShaderHash;
		fs >> result.Includes;

		// Test if includes sources are not newer than the cached file
		for (std::string& include : result.Includes)
		{
			if (!TestFileTime(include.c_str()))
			{
				result.Includes.clear();
				return false;
			}
		}

		uint32 size;
		fs >> size;

		char* pData = new char[size];
		fs.Read(pData, size);
		pUtils->CreateBlob(pData, size, DXC_CP_ACP, (IDxcBlobEncoding**)result.pBlob.GetAddressOf());
		delete[] pData;

		return true;
	}

	static bool SaveToCache(const char* pCachePath, const CompileJob& compileJob, CompileResult& result)
	{
		std::lock_guard lock(ShaderCacheMutex);

		Paths::CreateDirectoryTree(pCachePath);

		FileStream fs;
		fs.Open(pCachePath, FileMode::Write);
		uint32 version = CompileResult::Version;
		fs << version;
		fs << result.ShaderHash;
		fs << result.Includes;
		void* pBlob = result.pBlob->GetBufferPointer();
		uint32 size = (uint32)result.pBlob->GetBufferSize();
		fs << size;
		fs.Write(pBlob, size);
		return true;
	}

	static std::string CustomPreprocess(const std::string& input)
	{
		std::string output;
		output.reserve(input.size());

		size_t index = 0;
		constexpr const char* pSearchText = "TEXT(\"";
		constexpr uint32 searchLength = CString::StrLen(pSearchText);

		while (index < input.length())
		{
			size_t foundIndex = input.find(pSearchText, index);
			if (foundIndex != std::string::npos)
			{
				output += input.substr(index, foundIndex - index);

				// Find the closing parenthesis of the TEXT macro
				size_t closingQuoteIndex = input.find(')', foundIndex + searchLength);
				if (closingQuoteIndex != std::string::npos)
				{
					output += "{'";
					size_t targetStart = foundIndex + searchLength;
					size_t targetEnd = closingQuoteIndex - foundIndex - searchLength - 1;
					for (int i = 0; i < targetEnd; ++i)
					{
						output += input[targetStart + i];
						output += "','";
					}
					output.pop_back();
					output.pop_back();
					output += "}";

					index = closingQuoteIndex + 1;
				}
				else
				{
					output += input.substr(foundIndex, searchLength);
					index = foundIndex + searchLength;
				}
			}
			else
			{
				output += input.substr(index);
				break;
			}
		}
		return output;
	}

	static HRESULT TryLoadFile(const char* pFileName, Ref<IDxcBlobEncoding>* pOutFile)
	{
		HRESULT hr = E_FAIL;
		if (!Paths::FileExists(pFileName))
			return hr;

		uint64 temp, fileTime;
		Paths::GetFileTime(pFileName, temp, temp, fileTime);

		{
			std::lock_guard cacheLock(IncludeCacheMutex);
			auto it = IncludeCache.find(pFileName);
			if (it != IncludeCache.end())
			{
				CachedFile& file = it->second;
				if (fileTime <= file.Timestamp)
				{
					*pOutFile = file.pBlob;
					return S_OK;
				}
			}
		}

		FileStream stream;
		if(stream.Open(pFileName, FileMode::Read))
		{
			// +1 null terminator
			std::vector<char> charBuffer(stream.GetLength() + 1);
			stream.Read(charBuffer.data(), (uint32)charBuffer.size());
			std::string buffer = CustomPreprocess(charBuffer.data());

			CachedFile file;
			file.Timestamp = fileTime;
			hr = pUtils->CreateBlob(buffer.data(), (int)buffer.size(), 0, file.pBlob.GetAddressOf());
			if (SUCCEEDED(hr))
			{
				std::lock_guard cacheLock(IncludeCacheMutex);
				*pOutFile = file.pBlob;
				IncludeCache[pFileName] = file;
			}
		}

		return hr;
	}

	static CompileResult Compile(const CompileJob& compileJob)
	{
		CompileResult result;

		std::string defineKey;
		for (const ShaderDefine& define : compileJob.Defines)
			defineKey += define.Value;
		StringHash hash(defineKey.c_str());

		std::string cachePath = Sprintf(
			"%s%s_%s_%d_%d_%s_%x%s.bin",
			Paths::ShaderCacheDir().c_str(),
			Paths::GetFileNameWithoutExtension(compileJob.FilePath).c_str(),
			compileJob.Target.c_str(),
			(int)compileJob.MajVersion,
			(int)compileJob.MinVersion,
			compileJob.EntryPoint.c_str(),
			hash.m_Hash,
			compileJob.EnableDebugMode ? "_DEBUG" : ""
		);
		Paths::CreateDirectoryTree(cachePath);

		if (TryLoadFromCache(cachePath.c_str(), compileJob, result))
		{
			E_LOG(Info, "Loaded shader '%s.%s' from cache.", compileJob.FilePath.c_str(), compileJob.EntryPoint.c_str());
			return result;
		}

		Utils::TimeScope timer;
		Ref<IDxcBlobEncoding> pSource;
		std::string fullPath;
		if (!ResolveFilePath(compileJob, fullPath))
		{
			result.ErrorMessage = Sprintf("Failed to open file '%s'", compileJob.FilePath.c_str());
			return result;
		}

		if (!SUCCEEDED(TryLoadFile(fullPath.c_str(), &pSource)))
		{
			result.ErrorMessage = Sprintf("Failed to load file '%s'", fullPath.c_str());
			return result;
		}

		class CompileArguments
		{
		public:
			void AddArgument(const char* pArgument, const char* pValue = nullptr)
			{
				m_Arguments.push_back(MULTIBYTE_TO_UNICODE(pArgument));
				if (pValue)
					m_Arguments.push_back(MULTIBYTE_TO_UNICODE(pValue));
			}
			void AddArgument(const wchar_t* pArgument, const wchar_t* pValue = nullptr)
			{
				m_Arguments.push_back(pArgument);
				if (pValue)
					m_Arguments.push_back(pValue);
			}

			void AddDefine(const char* pDefine, const char* pValue = nullptr)
			{
				if (strstr(pDefine, "=") != nullptr)
					AddArgument("-D", pDefine);
				else
					AddArgument("-D", Sprintf("%s=%s", pDefine, pValue ? pValue : "1").c_str());
			}

			const wchar_t** GetArguments()
			{
				m_ArgumentArr.reserve(GetNumArguments());
				for (const auto& arg : m_Arguments)
					m_ArgumentArr.push_back(arg.c_str());
				return m_ArgumentArr.data();
			}

			size_t GetNumArguments() const
			{
				return m_Arguments.size();
			}

			std::string ToString() const
			{
				std::string str;
				for (const std::wstring& arg : m_Arguments)
					str += Sprintf(" %s", UNICODE_TO_MULTIBYTE(arg.c_str()));
				return str;
			}

		private:
			std::vector<const wchar_t*> m_ArgumentArr;
			std::vector<std::wstring> m_Arguments;
		} arguments;

		std::string target = Sprintf("%s_%d_%d", compileJob.Target.c_str(), compileJob.MajVersion, compileJob.MinVersion);
		arguments.AddArgument(Paths::GetFileNameWithoutExtension(compileJob.FilePath).c_str());
		arguments.AddArgument("-E", compileJob.EntryPoint.c_str());
		arguments.AddArgument("-T", target.c_str());
		arguments.AddArgument(DXC_ARG_ALL_RESOURCES_BOUND);
		arguments.AddArgument(DXC_ARG_WARNINGS_ARE_ERRORS);
		arguments.AddArgument(DXC_ARG_PACK_MATRIX_ROW_MAJOR);

		arguments.AddArgument("-HV", "2021");

#if 0
		if (majVersion >= 6 && minVersion >= 6)
		{
			arguments.AddArgument("-enable-payload-qualifiers");
			arguments.AddDefine("_PAYLOAD_QUALIFIERS", "1");
		}
		else
#endif
		{
			arguments.AddArgument("-disable-payload-qualifiers");
			arguments.AddDefine("_PAYLOAD_QUALIFIERS", "0");
		}

		result.IsDebug = compileJob.EnableDebugMode;

		arguments.AddArgument(DXC_ARG_DEBUG);
		arguments.AddArgument("-Qembed_debug");

		if (compileJob.EnableDebugMode)
			arguments.AddArgument(DXC_ARG_SKIP_OPTIMIZATIONS);

		arguments.AddArgument("-I", Paths::GetDirectoryPath(fullPath).c_str());
		for (const std::string& includeDir : compileJob.IncludeDirs)
			arguments.AddArgument("-I", includeDir.c_str());

		arguments.AddDefine(Sprintf("_SM_MAJ=%d", compileJob.MajVersion).c_str());
		arguments.AddDefine(Sprintf("_SM_MIN=%d", compileJob.MinVersion).c_str());
		arguments.AddDefine("_DXC");

		for (const ShaderDefine& define : compileJob.Defines)
			arguments.AddDefine(define.Value.c_str());

		DxcBuffer sourceBuffer;
		sourceBuffer.Ptr = pSource->GetBufferPointer();
		sourceBuffer.Size = pSource->GetBufferSize();
		sourceBuffer.Encoding = DXC_CP_ACP;

		class CustomIncludeHandler : public IDxcIncludeHandler
		{
		public:
			HRESULT STDMETHODCALLTYPE LoadSource(_In_ LPCWSTR pFilename, _COM_Outptr_result_maybenull_ IDxcBlob** ppIncludeSource) override
			{
				Ref<IDxcBlobEncoding> pEncoding;
				std::string path = Paths::Normalize(UNICODE_TO_MULTIBYTE(pFilename));
				check(Paths::ResolveRelativePaths(path));

				auto existingInclude = std::find_if(IncludedFiles.begin(), IncludedFiles.end(), [&path](const std::string& include) {
					return CString::StrCmp(include.c_str(), path.c_str(), false);
					});

				if (existingInclude != IncludedFiles.end())
				{
					static const char nullStr[] = " ";
					pUtils->CreateBlobFromPinned(nullStr, ARRAYSIZE(nullStr), DXC_CP_ACP, pEncoding.GetAddressOf());
					*ppIncludeSource = pEncoding.Detach();
					return S_OK;
				}

				HRESULT hr = TryLoadFile(UNICODE_TO_MULTIBYTE(pFilename), &pEncoding);
				if (SUCCEEDED(hr))
				{
					IncludedFiles.push_back(path);
					*ppIncludeSource = pEncoding.Detach();
				}
				return hr;
			}

			HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, _COM_Outptr_ void __RPC_FAR* __RPC_FAR* ppvObject) override {	return E_NOINTERFACE; }
			ULONG STDMETHODCALLTYPE AddRef(void) override { return 0; }
			ULONG STDMETHODCALLTYPE Release(void) override { return 0; }

			std::vector<std::string> IncludedFiles;
		};

		if (CommandLine::GetBool("dumpshaders"))
		{
			// Preprocessed source
			Ref<IDxcResult> pPreprocessOutput;
			CompileArguments preprocessArgs = arguments;
			preprocessArgs.AddArgument("-P", ".");
			CustomIncludeHandler preprocessIncludeHandler;
			if (SUCCEEDED(pCompiler3->Compile(&sourceBuffer, preprocessArgs.GetArguments(), (uint32)preprocessArgs.GetNumArguments(), &preprocessIncludeHandler, IID_PPV_ARGS(pPreprocessOutput.GetAddressOf()))))
			{
				Ref<IDxcBlobUtf8> pHLSL;
				if (SUCCEEDED(pPreprocessOutput->GetOutput(DXC_OUT_HLSL, IID_PPV_ARGS(pHLSL.GetAddressOf()), nullptr)))
				{
					std::string filePathBase = Paths::GetFileNameWithoutExtension(cachePath);
					{
						FileStream stream;
						if(stream.Open(Sprintf("%s%s.hlsl", Paths::ShaderCacheDir(), filePathBase).c_str(), FileMode::Write))
							stream.Write(pHLSL->GetStringPointer(), (uint32)pHLSL->GetStringLength());
					}
					{
						FileStream stream;
						if (stream.Open(Sprintf("%s%s.bat", Paths::ShaderCacheDir(), filePathBase).c_str(), FileMode::Write))
						{
							std::string txt = Sprintf("dxc.exe %s -Fo %s.shaderbin %s.hlsl", arguments.ToString(), filePathBase, filePathBase);
							stream.Write(txt.c_str(), (uint32)txt.size());
						}
					}
				}
			}
		}

		CustomIncludeHandler includeHandler;
		Ref<IDxcResult> pCompileResult;
		VERIFY_HR(pCompiler3->Compile(&sourceBuffer, arguments.GetArguments(), (uint32)arguments.GetNumArguments(), &includeHandler, IID_PPV_ARGS(pCompileResult.GetAddressOf())));

		Ref<IDxcBlobUtf8> pErrors;
		if (SUCCEEDED(pCompileResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(pErrors.GetAddressOf()), nullptr)))
		{
			if (pErrors && pErrors->GetStringLength())
				result.ErrorMessage = (char*)pErrors->GetStringPointer();
		}

		HRESULT hrStatus;
		if (FAILED(pCompileResult->GetStatus(&hrStatus)) || FAILED(hrStatus))
		{
			return result;
		}

		//Shader object
		{
			VERIFY_HR(pCompileResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(result.pBlob.GetAddressOf()), nullptr));
		}

		//Validation
		{
			Ref<IDxcOperationResult> pResult;
			VERIFY_HR(pValidator->Validate((IDxcBlob*)result.pBlob.Get(), DxcValidatorFlags_InPlaceEdit, pResult.GetAddressOf()));
			HRESULT validationResult;
			pResult->GetStatus(&validationResult);
			if (validationResult != S_OK)
			{
				Ref<IDxcBlobEncoding> pPrintBlob;
				Ref<IDxcBlobUtf8> pPrintBlobUtf8;
				pResult->GetErrorBuffer(pPrintBlob.GetAddressOf());
				pUtils->GetBlobAsUtf8(pPrintBlob.Get(), pPrintBlobUtf8.GetAddressOf());
				result.ErrorMessage = pPrintBlobUtf8->GetStringPointer();
				return result;
			}
		}

		// Hash
		{
			Ref<IDxcBlob> pHash;
			if (SUCCEEDED(pCompileResult->GetOutput(DXC_OUT_SHADER_HASH, IID_PPV_ARGS(pHash.GetAddressOf()), nullptr)))
			{
				DxcShaderHash* pHashBuf = (DxcShaderHash*)pHash->GetBufferPointer();
				memcpy(result.ShaderHash, pHashBuf->HashDigest, sizeof(uint64) * 2);
			}
		}

		//Symbols
#if 0
		{
			Ref<IDxcBlobUtf16> pDebugDataPath;
			Ref<IDxcBlob> pSymbolsBlob;
			if (SUCCEEDED(pCompileResult->GetOutput(DXC_OUT_PDB, IID_PPV_ARGS(pSymbolsBlob.GetAddressOf()), pDebugDataPath.GetAddressOf())))
			{
				Ref<IDxcBlobUtf8> pDebugDataPathUTF8;
				pUtils->GetBlobAsUtf8(pDebugDataPath.Get(), pDebugDataPathUTF8.GetAddressOf());
				std::string symbolsPath = Sprintf("%s%s", Paths::ShaderCacheDir().c_str(), pDebugDataPathUTF8->GetStringPointer());
				FileStream stream;
				if(stream.Open(symbolsPath.c_str(), FileMode::Write))
					stream.Write(pSymbolsBlob->GetBufferPointer(), pSymbolsBlob->GetBufferSize());
			}
		}
#endif

		//Reflection
		{
			Ref<IDxcBlob> pReflectionData;
			if (SUCCEEDED(pCompileResult->GetOutput(DXC_OUT_REFLECTION, IID_PPV_ARGS(pReflectionData.GetAddressOf()), nullptr)))
			{
				DxcBuffer reflectionBuffer;
				reflectionBuffer.Ptr = pReflectionData->GetBufferPointer();
				reflectionBuffer.Size = pReflectionData->GetBufferSize();
				reflectionBuffer.Encoding = 0;
				VERIFY_HR(pUtils->CreateReflection(&reflectionBuffer, IID_PPV_ARGS(result.pReflection.GetAddressOf())));
			}
		}

		result.Includes.push_back(fullPath);
		for (const std::string& includePath : includeHandler.IncludedFiles)
			result.Includes.push_back(includePath);

		check(SaveToCache(cachePath.c_str(), compileJob, result));
		E_LOG(Warning, "Missing cached shader. Compile time: %.1fms ('%s.%s')", timer.Stop() * 1000, compileJob.FilePath.c_str(), compileJob.EntryPoint.c_str());

		return result;
	}
}

ShaderManager::ShaderStringHash ShaderManager::GetEntryPointHash(const char* pEntryPoint, Span<ShaderDefine> defines)
{
	ShaderStringHash hash(pEntryPoint);
	for (const ShaderDefine& define : defines)
		hash.Combine(ShaderStringHash(define.Value));
	return hash;
}

void ShaderManager::RecompileFromFileChange(const std::string& filePath)
{
	auto it = m_IncludeDependencyMap.find(ShaderStringHash(filePath));
	if (it != m_IncludeDependencyMap.end())
	{
		E_LOG(Info, "Modified \"%s\". Dirtying dependent shaders...", filePath.c_str());
		const std::unordered_set<std::string>& dependencies = it->second;
		for (const std::string& dependency : dependencies)
		{
			auto objectMapIt = m_FilepathToObjectMap.find(ShaderStringHash(dependency));
			if (objectMapIt != m_FilepathToObjectMap.end())
			{
				ShadersInFileMap objectMap = objectMapIt->second;
				for (auto shader : objectMap.Shaders)
				{
					shader.second->IsDirty = true;
					if (shader.second)
						m_OnShaderEditedEvent.Broadcast(shader.second);
				}
			}
		}
	}
}

ShaderManager::ShaderManager(uint8 shaderModelMaj, uint8 shaderModelMin)
	: m_ShaderModelMajor(shaderModelMaj), m_ShaderModelMinor(shaderModelMin)
{
	m_pFileWatcher = std::make_unique<FileWatcher>();
	ShaderCompiler::LoadDXC();
}

ShaderManager::~ShaderManager()
{
	for (Shader* pShader : m_Shaders)
		delete pShader;
}

void ShaderManager::ConditionallyReloadShaders()
{
	if (m_pFileWatcher)
	{
		FileEvent fileEvent;
		while (m_pFileWatcher->GetNextChange(fileEvent))
		{
			switch (fileEvent.EventType)
			{
			case FileEvent::Type::Modified:
				RecompileFromFileChange(fileEvent.Path);
				break;
			case FileEvent::Type::Added:
				break;
			case FileEvent::Type::Removed:
				break;
			}
		}
	}
}

void ShaderManager::AddIncludeDir(const std::string& includeDir)
{
	m_IncludeDirs.push_back(includeDir);
	if (m_pFileWatcher)
	{
		if (m_pFileWatcher->StartWatching(includeDir.c_str(), true))
			E_LOG(Info, "Shader Hot-Reload enabled for: \"%s\"", includeDir.c_str());
		else
			E_LOG(Warning, "Shader Hot-Reload for \"%s\" failed.", includeDir.c_str());
	}
}

ShaderResult ShaderManager::GetShader(const char* pShaderPath, ShaderType shaderType, const char* pEntryPoint, Span<ShaderDefine> defines /*= {}*/)
{
	std::lock_guard lock(m_CompileMutex);

	// Libs have no entry point
	if (!pEntryPoint)
		pEntryPoint = "";

	ShaderStringHash pathHash(pShaderPath);
	ShaderStringHash hash = GetEntryPointHash(pEntryPoint, defines);

	Shader* pShader = nullptr;

	{
		auto& shaderMap = m_FilepathToObjectMap[pathHash].Shaders;
		auto it = shaderMap.find(hash);
		if (it != shaderMap.end())
			pShader = it->second;
	}

	if (pShader && !pShader->IsDirty)
		return { pShader, "" };

	ShaderCompiler::CompileJob job;
	job.Defines = defines;
	job.EntryPoint = pEntryPoint;
	job.FilePath = pShaderPath;
	job.IncludeDirs = m_IncludeDirs;
	job.MajVersion = m_ShaderModelMajor;
	job.MinVersion = m_ShaderModelMinor;
	job.Target = ShaderCompiler::GetShaderTarget(shaderType);
	job.EnableDebugMode = CommandLine::GetBool("debugshaders");

	ShaderCompiler::CompileResult result = ShaderCompiler::Compile(job);

	if (!result.Success())
	{
		std::string error = Sprintf("Failed to compile shader \"%s:%s\": %s", pShaderPath, pEntryPoint, result.ErrorMessage.c_str());
		E_LOG(Warning, "%s", error);
		return { nullptr, error };
	}

	{
		if (!pShader)
			pShader = m_Shaders.emplace_back(new Shader());

		pShader->Defines = defines.Copy();
		pShader->EntryPoint = pEntryPoint;
		pShader->Type = shaderType;
		pShader->pByteCode = result.pBlob;
		pShader->IsDirty = false;
		memcpy(pShader->Hash, result.ShaderHash, sizeof(uint64) * 2);

		for (const std::string& include : result.Includes)
			m_IncludeDependencyMap[ShaderStringHash(include)].insert(pShaderPath);
		m_FilepathToObjectMap[pathHash].Shaders[hash] = pShader;
	}
	return { pShader };
}
