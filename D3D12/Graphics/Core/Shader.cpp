#include "stdafx.h"
#include "Shader.h"
#include "Core/Paths.h"
#include "Core/CommandLine.h"
#include "Core/FileWatcher.h"
#include "dxc/dxcapi.h"

namespace ShaderCompiler
{
	constexpr const char* pShaderSymbolsPath = "Saved/ShaderSymbols/";

	static ComPtr<IDxcUtils> pUtils;
	static ComPtr<IDxcCompiler3> pCompiler3;
	static ComPtr<IDxcValidator> pValidator;
	static ComPtr<IDxcIncludeHandler> pDefaultIncludeHandler;

	struct CompileResult
	{
		std::string ErrorMessage;
		std::string DebugPath;
		std::string PreprocessedSource;
		ShaderBlob pBlob;
		ShaderBlob pSymbolsBlob;
		ComPtr<IUnknown> pReflection;
		std::vector<std::string> Includes;

		bool Success() const { return pBlob.Get() && ErrorMessage.length() == 0; }
	};

	constexpr const char* GetShaderTarget(ShaderType type)
	{
		switch (type)
		{
		case ShaderType::Vertex:		return "vs";
		case ShaderType::Pixel:			return "ps";
		case ShaderType::Geometry:		return "gs";
		case ShaderType::Compute:		return "cs";
		case ShaderType::Hull:			return "hs";
		case ShaderType::Domain:		return "ds";
		case ShaderType::Mesh:			return "ms";
		case ShaderType::Amplification: return "as";
		default: noEntry();				return "";
		}
	}

	void LoadDXC()
	{
		HMODULE lib = LoadLibraryA("dxcompiler.dll");
		check(lib);
		DxcCreateInstanceProc createInstance = (DxcCreateInstanceProc)GetProcAddress(lib, "DxcCreateInstance");
		check(createInstance);
		VERIFY_HR(createInstance(CLSID_DxcUtils, IID_PPV_ARGS(pUtils.GetAddressOf())));
		VERIFY_HR(createInstance(CLSID_DxcCompiler, IID_PPV_ARGS(pCompiler3.GetAddressOf())));
		VERIFY_HR(createInstance(CLSID_DxcValidator, IID_PPV_ARGS(pValidator.GetAddressOf())));
		VERIFY_HR(pUtils->CreateDefaultIncludeHandler(pDefaultIncludeHandler.GetAddressOf()));
		E_LOG(Info, "Loaded dxcompiler.dll");
	}

	CompileResult CompileDxc(const char* pFilePath, const char* pEntryPoint, const char* pTarget, uint8 majVersion, uint8 minVersion, const std::vector<ShaderDefine>& defines)
	{
		CompileResult result;

		ComPtr<IDxcBlobEncoding> pSource;
		if (!SUCCEEDED(pUtils->LoadFile(MULTIBYTE_TO_UNICODE(pFilePath), nullptr, pSource.GetAddressOf())))
		{
			result.ErrorMessage = Sprintf("Failed to open file '%s'", pFilePath);
			return result;
		}

		bool debugShaders = CommandLine::GetBool("debugshaders");
		bool shaderSymbols = CommandLine::GetBool("shadersymbols");

		std::string target = Sprintf("%s_%d_%d", pTarget, majVersion, minVersion);

		class CompileArguments
		{
		public:
			void AddArgument(const char* pArgument, const char* pValue = nullptr)
			{
				auto it = argumentStrings.insert(MULTIBYTE_TO_UNICODE(pArgument));
				pArguments.push_back(it.first->c_str());
				if (pValue)
				{
					it = argumentStrings.insert(MULTIBYTE_TO_UNICODE(pValue));
					pArguments.push_back(it.first->c_str());
				}
			}
			void AddArgument(const wchar_t* pArgument, const wchar_t* pValue = nullptr)
			{
				auto it = argumentStrings.insert(pArgument);
				pArguments.push_back(it.first->c_str());
				if (pValue)
				{
					it = argumentStrings.insert(pValue);
					pArguments.push_back(it.first->c_str());
				}
			}

			void AddDefine(const char* pDefine, const char* pValue = nullptr)
			{
				if (strstr(pDefine, "=") != nullptr)
				{
					AddArgument("-D", pDefine);
				}
				else
				{
					AddArgument("-D", Sprintf("%s=%s", pDefine, pValue ? pValue : "1").c_str());
				}
			}

			const wchar_t** GetArguments() { return pArguments.data(); }
			size_t GetNumArguments() const { return pArguments.size(); }
		private:
			std::vector<const wchar_t*> pArguments;
			std::unordered_set<std::wstring> argumentStrings;
		} arguments;

		arguments.AddArgument(Paths::GetFileNameWithoutExtension(pFilePath).c_str());
		arguments.AddArgument("-E", pEntryPoint);
		arguments.AddArgument("-T", target.c_str());
		arguments.AddArgument("-enable-templates");
		arguments.AddArgument(DXC_ARG_ALL_RESOURCES_BOUND);
#if 0
		if (majVersion >= 6 && minVersion >= 6)
		{
			arguments.AddArgument("-enable-payload-qualifiers");
			arguments.AddDefine("_PAYLOAD_QUALIFIERS", "1");
		}
		else
#endif
		{
			arguments.AddDefine("_PAYLOAD_QUALIFIERS", "0");
		}

		if (debugShaders || shaderSymbols)
		{
			arguments.AddArgument("-Qembed_debug");
			arguments.AddArgument(DXC_ARG_DEBUG);
		}
		else
		{
			arguments.AddArgument("-Qstrip_debug");
			arguments.AddArgument("-Fd", pShaderSymbolsPath);
			arguments.AddArgument("-Qstrip_reflect");
		}

		if (debugShaders)
		{
			arguments.AddArgument(DXC_ARG_SKIP_OPTIMIZATIONS);
		}
		else
		{
			arguments.AddArgument(DXC_ARG_OPTIMIZATION_LEVEL3);
		}

		arguments.AddArgument(DXC_ARG_WARNINGS_ARE_ERRORS);
		arguments.AddArgument(DXC_ARG_PACK_MATRIX_ROW_MAJOR);

		std::string shaderDir = Paths::GetDirectoryPath(pFilePath);
		arguments.AddArgument("-I", shaderDir.c_str());
		arguments.AddArgument("-I", Paths::Combine(shaderDir, "include").c_str());

		for (const ShaderDefine& define : defines)
		{
			arguments.AddDefine(define.Value.c_str());
		}

		DxcBuffer sourceBuffer;
		sourceBuffer.Ptr = pSource->GetBufferPointer();
		sourceBuffer.Size = pSource->GetBufferSize();
		sourceBuffer.Encoding = 0;

		class CustomIncludeHandler : public IDxcIncludeHandler
		{
		public:
			HRESULT STDMETHODCALLTYPE LoadSource(_In_ LPCWSTR pFilename, _COM_Outptr_result_maybenull_ IDxcBlob** ppIncludeSource) override
			{
				ComPtr<IDxcBlobEncoding> pEncoding;
				std::string path = Paths::Normalize(UNICODE_TO_MULTIBYTE(pFilename));
				if (!Paths::FileExists(path))
				{
					return E_FAIL;
				}

				if (IncludedFiles.find(path) != IncludedFiles.end())
				{
					static const char nullStr[] = " ";
					pUtils->CreateBlob(nullStr, ARRAYSIZE(nullStr), CP_UTF8, pEncoding.GetAddressOf());
					*ppIncludeSource = pEncoding.Detach();
					return S_OK;
				}

				HRESULT hr = pUtils->LoadFile(pFilename, nullptr, pEncoding.GetAddressOf());
				if (SUCCEEDED(hr))
				{
					IncludedFiles.insert(path);
					*ppIncludeSource = pEncoding.Detach();
				}
				else
				{
					*ppIncludeSource = nullptr;
				}
				return hr;
			}

			HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, _COM_Outptr_ void __RPC_FAR* __RPC_FAR* ppvObject) override
			{
				return pDefaultIncludeHandler->QueryInterface(riid, ppvObject);
			}

			ULONG STDMETHODCALLTYPE AddRef(void) override {	return 0; }
			ULONG STDMETHODCALLTYPE Release(void) override { return 0; }

			std::unordered_set<std::string> IncludedFiles;
		} includeHandler;

		ComPtr<IDxcResult> pCompileResult;
		VERIFY_HR(pCompiler3->Compile(&sourceBuffer, arguments.GetArguments(), (uint32)arguments.GetNumArguments(), &includeHandler, IID_PPV_ARGS(pCompileResult.GetAddressOf())));
#if 0
		// Preprocessed source
		ComPtr<IDxcResult> pPreprocessOutput;
		arguments.AddArgument("-P", ".");
		if (SUCCEEDED(pCompiler3->Compile(&sourceBuffer, arguments.GetArguments(), (uint32)arguments.GetNumArguments(), &includeHandler, IID_PPV_ARGS(pPreprocessOutput.GetAddressOf()))))
		{
			ComPtr<IDxcBlobUtf8> pHLSL;
			pPreprocessOutput->GetOutput(DXC_OUT_HLSL, IID_PPV_ARGS(pHLSL.GetAddressOf()), nullptr);
			if (pHLSL && pHLSL->GetStringLength() > 0)
			{
				result.ShaderSource = (char*)pHLSL->GetBufferPointer();
			}
		}
#endif

		ComPtr<IDxcBlobUtf8> pErrors;
		pCompileResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(pErrors.GetAddressOf()), nullptr);
		if (pErrors && pErrors->GetStringLength() > 0)
		{
			result.ErrorMessage = (char*)pErrors->GetBufferPointer();
			return result;
		}

		//Shader object
		{
			VERIFY_HR(pCompileResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(result.pBlob.GetAddressOf()), nullptr));
		}

		//Validation
		{
			ComPtr<IDxcOperationResult> pResult;
			VERIFY_HR(pValidator->Validate((IDxcBlob*)result.pBlob.Get(), DxcValidatorFlags_InPlaceEdit, pResult.GetAddressOf()));
			HRESULT validationResult;
			pResult->GetStatus(&validationResult);
			if (validationResult != S_OK)
			{
				ComPtr<IDxcBlobEncoding> pPrintBlob;
				ComPtr<IDxcBlobUtf8> pPrintBlobUtf8;
				pResult->GetErrorBuffer(pPrintBlob.GetAddressOf());
				pUtils->GetBlobAsUtf8(pPrintBlob.Get(), pPrintBlobUtf8.GetAddressOf());
				result.ErrorMessage = (char*)pPrintBlobUtf8->GetBufferPointer();
				return result;
			}
		}

		//Symbols
		{
			ComPtr<IDxcBlobUtf16> pDebugDataPath;
			if (SUCCEEDED(pCompileResult->GetOutput(DXC_OUT_PDB, IID_PPV_ARGS(result.pSymbolsBlob.GetAddressOf()), pDebugDataPath.GetAddressOf())))
			{
				result.DebugPath = std::string(pShaderSymbolsPath) + UNICODE_TO_MULTIBYTE((wchar_t*)pDebugDataPath->GetBufferPointer());
			}
		}

		//Reflection
		{
			ComPtr<IDxcBlob> pReflectionData;
			if (SUCCEEDED(pCompileResult->GetOutput(DXC_OUT_REFLECTION, IID_PPV_ARGS(pReflectionData.GetAddressOf()), nullptr)))
			{
				DxcBuffer reflectionBuffer;
				reflectionBuffer.Ptr = pReflectionData->GetBufferPointer();
				reflectionBuffer.Size = pReflectionData->GetBufferSize();
				reflectionBuffer.Encoding = 0;
				VERIFY_HR(pUtils->CreateReflection(&reflectionBuffer, IID_PPV_ARGS(result.pReflection.GetAddressOf())));
			}
		}

		for (const std::string& includePath : includeHandler.IncludedFiles)
		{
			result.Includes.push_back(includePath);
		}

		return result;
	}

	CompileResult Compile(const char* pFilePath, const char* pTarget, const char* pEntryPoint, uint8 majVersion, uint8 minVersion, const std::vector<ShaderDefine>& defines /*= {}*/)
	{
		std::vector<ShaderDefine> definesActual = defines;
		definesActual.push_back(Sprintf("_SM_MAJ=%d", majVersion));
		definesActual.push_back(Sprintf("_SM_MIN=%d", minVersion));
		definesActual.emplace_back("_DXC=1");
		return CompileDxc(pFilePath, pEntryPoint, pTarget, majVersion, minVersion, definesActual);
	}
}

ShaderBase::~ShaderBase()
{

}

ShaderManager::ShaderStringHash ShaderManager::GetEntryPointHash(const char* pEntryPoint, const std::vector<ShaderDefine>& defines)
{
	StringHash hash(pEntryPoint);
	for (const ShaderDefine& define : defines)
	{
		hash.Combine(StringHash(define.Value));
	}
	return hash;
}

Shader* ShaderManager::LoadShader(const char* pShaderPath, ShaderType shaderType, const char* pEntryPoint, const std::vector<ShaderDefine>& defines /*= {}*/)
{
	std::string filePath = Paths::Combine(m_pShaderSourcePath, pShaderPath);

	ShaderCompiler::CompileResult result = ShaderCompiler::Compile(
		filePath.c_str(),
		ShaderCompiler::GetShaderTarget(shaderType),
		pEntryPoint,
		m_ShaderModelMajor,
		m_ShaderModelMinor,
		defines);

	if (!result.Success())
	{
		E_LOG(Warning, "Failed to compile shader \"%s:%s\": %s", pShaderPath, pEntryPoint, result.ErrorMessage.c_str());
		return nullptr;
	}

	ShaderPtr pNewShader = std::make_unique<Shader>(result.pBlob, shaderType, pEntryPoint, defines);
	m_Shaders.push_back(std::move(pNewShader));
	Shader* pShader = m_Shaders.back().get();

	for (const std::string& include : result.Includes)
	{
		m_IncludeDependencyMap[ShaderStringHash(Paths::GetFileName(include))].insert(pShaderPath);
	}
	m_IncludeDependencyMap[ShaderStringHash(pShaderPath)].insert(pShaderPath);

	StringHash hash = GetEntryPointHash(pEntryPoint, defines);
	m_FilepathToObjectMap[ShaderStringHash(pShaderPath)].Shaders[hash] = pShader;

	return pShader;
}

ShaderLibrary* ShaderManager::LoadShaderLibrary(const char* pShaderPath, const std::vector<ShaderDefine>& defines /*= {}*/)
{
	std::string filePath = Paths::Combine(m_pShaderSourcePath, pShaderPath);

	ShaderCompiler::CompileResult result = ShaderCompiler::Compile(
		filePath.c_str(),
		"lib",
		"",
		m_ShaderModelMajor,
		m_ShaderModelMinor,
		defines);

	if (!result.Success())
	{
		E_LOG(Warning, "Failed to compile library \"%s\": %s", pShaderPath, result.ErrorMessage.c_str());
		return nullptr;
	}

	LibraryPtr pNewShader = std::make_unique<ShaderLibrary>(result.pBlob, defines);
	m_Libraries.push_back(std::move(pNewShader));
	ShaderLibrary* pLibrary = m_Libraries.back().get();

	for (const std::string& include : result.Includes)
	{
		m_IncludeDependencyMap[ShaderStringHash(Paths::GetFileName(include))].insert(pShaderPath);
	}
	m_IncludeDependencyMap[ShaderStringHash(pShaderPath)].insert(pShaderPath);

	StringHash hash = GetEntryPointHash("", defines);
	m_FilepathToObjectMap[ShaderStringHash(pShaderPath)].Libraries[hash] = pLibrary;

	return pLibrary;
}

void ShaderManager::RecompileFromFileChange(const std::string& filePath)
{
	auto it = m_IncludeDependencyMap.find(ShaderStringHash(filePath));
	if (it != m_IncludeDependencyMap.end())
	{
		E_LOG(Info, "Modified \"%s\". Recompiling dependencies...", filePath.c_str());
		const std::unordered_set<std::string>& dependencies = it->second;
		for (const std::string& dependency : dependencies)
		{
			auto objectMapIt = m_FilepathToObjectMap.find(ShaderStringHash(dependency));
			if (objectMapIt != m_FilepathToObjectMap.end())
			{
				ShadersInFileMap objectMap = objectMapIt->second;
				for (auto shader : objectMap.Shaders)
				{
					Shader* pOldShader = shader.second;
					Shader* pNewShader = LoadShader(dependency.c_str(), pOldShader->GetType(), pOldShader->GetEntryPoint(), pOldShader->GetDefines());
					if (pNewShader)
					{
						E_LOG(Info, "Reloaded shader: \"%s - %s\"", dependency.c_str(), pNewShader->GetEntryPoint());
						m_OnShaderRecompiledEvent.Broadcast(pOldShader, pNewShader);
						m_Shaders.remove_if([pOldShader](const std::unique_ptr<Shader>& pS) { return pS.get() == pOldShader; });
					}
					else
					{
						E_LOG(Warning, "Failed to reload shader: \"%s\"", dependency.c_str());
					}
				}
				for (auto library : objectMap.Libraries)
				{
					ShaderLibrary* pOldLibrary = library.second;
					ShaderLibrary* pNewLibrary = LoadShaderLibrary(dependency.c_str(), pOldLibrary->GetDefines());
					if (pNewLibrary)
					{
						E_LOG(Info, "Reloaded library: \"%s\"", dependency.c_str());
						m_OnLibraryRecompiledEvent.Broadcast(pOldLibrary, pNewLibrary);
						m_Libraries.remove_if([pOldLibrary](const std::unique_ptr<ShaderLibrary>& pS) { return pS.get() == pOldLibrary; });
					}
					else
					{
						E_LOG(Warning, "Failed to reload library: \"%s\"", dependency.c_str());
					}
				}
			}
		}
	}
}

void* ShaderBase::GetByteCode() const
{
	return m_pByteCode->GetBufferPointer();
}

uint32 ShaderBase::GetByteCodeSize() const
{
	return (uint32)m_pByteCode->GetBufferSize();
}

ShaderManager::ShaderManager(const char* pShaderSourcePath, uint8 shaderModelMaj, uint8 shaderModelMin)
	: m_pShaderSourcePath(pShaderSourcePath), m_ShaderModelMajor(shaderModelMaj), m_ShaderModelMinor(shaderModelMin)
{
	ShaderCompiler::LoadDXC();
	if (CommandLine::GetBool("shaderhotreload"))
	{
		m_pFileWatcher = std::make_unique<FileWatcher>();
		m_pFileWatcher->StartWatching(pShaderSourcePath, true);
		E_LOG(Info, "Shader Hot-Reload enabled: \"%s\"", pShaderSourcePath);
	}
}

ShaderManager::~ShaderManager()
{
}

void ShaderManager::ConditionallyReloadShaders()
{
	if (m_pFileWatcher)
	{
		FileWatcher::FileEvent fileEvent;
		while (m_pFileWatcher->GetNextChange(fileEvent))
		{
			switch (fileEvent.EventType)
			{
			case FileWatcher::FileEvent::Type::Modified:
				RecompileFromFileChange(fileEvent.Path);
				break;
			case FileWatcher::FileEvent::Type::Added:
				break;
			case FileWatcher::FileEvent::Type::Removed:
				break;
			}
		}
	}
}

Shader* ShaderManager::GetShader(const char* pShaderPath, ShaderType shaderType, const char* pEntryPoint, const std::vector<ShaderDefine>& defines /*= {}*/)
{
	auto& shaderMap = m_FilepathToObjectMap[ShaderStringHash(pShaderPath)].Shaders;
	StringHash hash = GetEntryPointHash(pEntryPoint, defines);
	auto it = shaderMap.find(hash);
	if (it != shaderMap.end())
	{
		return it->second;
	}

	return LoadShader(pShaderPath, shaderType, pEntryPoint, defines);
}

ShaderLibrary* ShaderManager::GetLibrary(const char* pShaderPath, const std::vector<ShaderDefine>& defines /*= {}*/)
{
	auto& libraryMap = m_FilepathToObjectMap[ShaderStringHash(pShaderPath)].Libraries;
	StringHash hash = GetEntryPointHash("", defines);
	auto it = libraryMap.find(hash);
	if (it != libraryMap.end())
	{
		return it->second;
	}

	return LoadShaderLibrary(pShaderPath, defines);
}
