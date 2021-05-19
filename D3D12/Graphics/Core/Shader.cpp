#include "stdafx.h"
#include "Shader.h"
#include "Core/Paths.h"
#include "Core/CommandLine.h"

#include <D3Dcompiler.h>
#include "Core/FileWatcher.h"

#ifndef USE_SHADER_LINE_DIRECTIVE
#define USE_SHADER_LINE_DIRECTIVE 1
#endif

namespace ShaderCompiler
{
	constexpr const char* pShaderSymbolsPath = "_Temp/ShaderSymbols/";

	struct CompileResult
	{
		bool Success = false;
		std::string ErrorMessage;
		std::string DebugPath;
		ShaderBlob pBlob;
		ShaderBlob pSymbolsBlob;
		ComPtr<IUnknown> pReflection;
	};

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

		void AddDefine(const char* pDefine)
		{
			AddArgument(Sprintf("-D %s", pDefine).c_str());
		}
		const wchar_t** GetArguments() { return pArguments.data(); }
		size_t GetNumArguments() const { return argumentStrings.size(); }
	private:
		std::vector<const wchar_t*> pArguments;
		std::unordered_set<std::wstring> argumentStrings;
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

	CompileResult CompileDxc(const char* pIdentifier, const char* pShaderSource, uint32 shaderSourceSize, const char* pEntryPoint, const char* pTarget, uint8 majVersion, uint8 minVersion, const std::vector<std::string>& defines)
	{
		static ComPtr<IDxcUtils> pUtils;
		static ComPtr<IDxcCompiler3> pCompiler;
		static ComPtr<IDxcValidator> pValidator;
		if (!pUtils)
		{
			VERIFY_HR(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(pUtils.GetAddressOf())));
			VERIFY_HR(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(pCompiler.GetAddressOf())));
			VERIFY_HR(DxcCreateInstance(CLSID_DxcValidator, IID_PPV_ARGS(pValidator.GetAddressOf())));
		}

		ComPtr<IDxcBlobEncoding> pSource;
		VERIFY_HR(pUtils->CreateBlobFromPinned(pShaderSource, shaderSourceSize, CP_UTF8, pSource.GetAddressOf()));

		bool debugShaders = CommandLine::GetBool("debugshaders");
		bool shaderSymbols = CommandLine::GetBool("shadersymbols");

		std::string target = Sprintf("%s_%d_%d", pTarget, majVersion, minVersion);

		CompileArguments arguments;
		arguments.AddArgument(pIdentifier);
		arguments.AddArgument("-E", pEntryPoint);
		arguments.AddArgument("-T", target.c_str());
		arguments.AddArgument("-enable-templates");
		arguments.AddArgument(DXC_ARG_ALL_RESOURCES_BOUND);

#if 0 // #todo: PAQ seems broken right now... :(
		if (majVersion >= 6 && minVersion >= 6)
		{
			arguments.AddArgument("-enable-payload-qualifiers");
			arguments.AddDefine("_PAYLOAD_QUALIFIERS=1");
		}
		else
#endif
		{
			arguments.AddDefine("_PAYLOAD_QUALIFIERS=0");
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

		for (const std::string& define : defines)
		{
			arguments.AddDefine(define.c_str());
		}

		DxcBuffer sourceBuffer;
		sourceBuffer.Ptr = pSource->GetBufferPointer();
		sourceBuffer.Size = pSource->GetBufferSize();
		sourceBuffer.Encoding = 0;

		ComPtr<IDxcResult> pCompileResult;
		VERIFY_HR(pCompiler->Compile(&sourceBuffer, arguments.GetArguments(), (uint32)arguments.GetNumArguments(), nullptr, IID_PPV_ARGS(pCompileResult.GetAddressOf())));

		CompileResult result;

		ComPtr<IDxcBlobUtf8> pErrors;
		pCompileResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(pErrors.GetAddressOf()), nullptr);
		if (pErrors && pErrors->GetStringLength() > 0)
		{
			result.Success = false;
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
			VERIFY_HR(pValidator->Validate(*result.pBlob.GetAddressOf(), DxcValidatorFlags_InPlaceEdit, pResult.GetAddressOf()));
			HRESULT validationResult;
			pResult->GetStatus(&validationResult);
			if (validationResult != S_OK)
			{
				ComPtr<IDxcBlobEncoding> pPrintBlob;
				ComPtr<IDxcBlobUtf8> pPrintBlobUtf8;
				pResult->GetErrorBuffer(pPrintBlob.GetAddressOf());
				pUtils->GetBlobAsUtf8(pPrintBlob.Get(), pPrintBlobUtf8.GetAddressOf());
				result.ErrorMessage = (char*)pPrintBlobUtf8->GetBufferPointer();
				result.Success = false;
				return result;
			}
		}

		result.Success = true;

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

		return result;
	}

	CompileResult CompileFxc(const char* pIdentifier, const char* pShaderSource, uint32 shaderSourceSize, const char* pEntryPoint, const char* pTarget, uint8 majVersion, uint8 minVersion, const std::vector<std::string>& defines)
	{
		bool debugShaders = CommandLine::GetBool("debugshaders");

		uint32 compileFlags = D3DCOMPILE_PACK_MATRIX_ROW_MAJOR;

		if (debugShaders)
		{
			// Enable better shader debugging with the graphics debugging tools.
			compileFlags |= D3DCOMPILE_DEBUG;
			compileFlags |= D3DCOMPILE_SKIP_OPTIMIZATION;
			compileFlags |= D3DCOMPILE_PREFER_FLOW_CONTROL;
		}
		else
		{
			compileFlags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
		}

		std::vector<std::pair<std::string, std::string>> defineValues(defines.size());
		std::vector<D3D_SHADER_MACRO> shaderDefines;
		for (size_t i = 0; i < defines.size(); ++i)
		{
			D3D_SHADER_MACRO m;
			const std::string& define = defines[i];
			defineValues[i] = std::make_pair<std::string,std::string>(define.substr(0, define.find('=')), define.substr(define.find('=') + 1));
			m.Name = defineValues[i].first.c_str();
			m.Definition = defineValues[i].second.c_str();
			shaderDefines.push_back(m);
		}

		D3D_SHADER_MACRO endMacro;
		endMacro.Name = nullptr;
		endMacro.Definition = nullptr;
		shaderDefines.push_back(endMacro);

		CompileResult result;

		ComPtr<ID3DBlob> pErrorBlob;
		std::string target = Sprintf("%s_%d_%d", pTarget, majVersion, minVersion);
		if (SUCCEEDED(D3DCompile(pShaderSource, shaderSourceSize, pIdentifier, shaderDefines.data(), nullptr, pEntryPoint, target.c_str(), compileFlags, 0, (ID3DBlob**)result.pBlob.GetAddressOf(), pErrorBlob.GetAddressOf())) != S_OK)
		{
			result.Success = true;
			D3DReflect(result.pBlob->GetBufferPointer(), result.pBlob->GetBufferSize(), IID_PPV_ARGS(result.pReflection.GetAddressOf()));
		}
		else
		{
			if (pErrorBlob != nullptr)
			{
				result.ErrorMessage = (char*)pErrorBlob->GetBufferPointer();
				result.Success = false;
			}
		}
		return result;
	}

	CompileResult Compile(const char* pIdentifier, const char* pShaderSource, uint32 shaderSourceSize, const char* pTarget, const char* pEntryPoint, uint8 majVersion, uint8 minVersion, const std::vector<ShaderDefine>& defines /*= {}*/)
	{
		std::vector<std::string> definesActual;
		for (const ShaderDefine& define : defines)
		{
			definesActual.push_back(define.Value);
		}

		for (std::string& define : definesActual)
		{
			if (define.find('=') == std::string::npos)
			{
				define += std::string("=1");
			}
		}

		definesActual.push_back(Sprintf("_SM_MAJ=%d", majVersion));
		definesActual.push_back(Sprintf("_SM_MIN=%d", minVersion));

		if (majVersion < 6)
		{
			definesActual.emplace_back("_FXC=1");
			return CompileFxc(pIdentifier, pShaderSource, shaderSourceSize, pEntryPoint, pTarget, majVersion, minVersion, definesActual);
		}
		definesActual.emplace_back("_DXC=1");
		return CompileDxc(pIdentifier, pShaderSource, shaderSourceSize, pEntryPoint, pTarget, majVersion, minVersion, definesActual);
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
	std::vector<ShaderStringHash> includes;
	char filePath[1024];
	FormatString(filePath, ARRAYSIZE(filePath), "%s%s", m_pShaderSourcePath, pShaderPath);

	std::stringstream shaderSource;
	if (!ProcessSource(filePath, filePath, shaderSource, includes))
	{
		return nullptr;
	}

	std::string source = shaderSource.str();
	ShaderCompiler::CompileResult result = ShaderCompiler::Compile(
		pShaderPath,
		source.c_str(),
		(uint32)source.size(),
		ShaderCompiler::GetShaderTarget(shaderType),
		pEntryPoint,
		m_ShaderModelMajor,
		m_ShaderModelMinor,
		defines);

	if (!result.Success)
	{
		E_LOG(Warning, "Failed to compile shader \"%s\": %s", pShaderPath, result.ErrorMessage.c_str());
		return nullptr;
	}

	ShaderPtr pNewShader = std::make_unique<Shader>(result.pBlob, shaderType, pEntryPoint, defines);
	m_Shaders.push_back(std::move(pNewShader));
	Shader* pShader = m_Shaders.back().get();

	for (const ShaderStringHash& include : includes)
	{
		m_IncludeDependencyMap[include].insert(pShaderPath);
	}
	m_IncludeDependencyMap[ShaderStringHash(pShaderPath)].insert(pShaderPath);

	StringHash hash = GetEntryPointHash(pEntryPoint, defines);
	m_FilepathToObjectMap[ShaderStringHash(pShaderPath)].Shaders[hash] = pShader;

	return pShader;
}

ShaderLibrary* ShaderManager::LoadShaderLibrary(const char* pShaderPath, const std::vector<ShaderDefine>& defines /*= {}*/)
{
	std::stringstream shaderSource;
	std::vector<ShaderStringHash> includes;
	char filePath[1024];
	FormatString(filePath, ARRAYSIZE(filePath), "%s%s", m_pShaderSourcePath, pShaderPath);
	if (!ProcessSource(filePath, filePath, shaderSource, includes))
	{
		return nullptr;
	}

	std::string source = shaderSource.str();
	ShaderCompiler::CompileResult result = ShaderCompiler::Compile(
		pShaderPath,
		source.c_str(),
		(uint32)source.size(),
		"lib",
		"",
		m_ShaderModelMajor,
		m_ShaderModelMinor,
		defines);

	if (!result.Success)
	{
		E_LOG(Warning, "Failed to compile library \"%s\": %s", pShaderPath, result.ErrorMessage.c_str());
		return nullptr;
	}

	LibraryPtr pNewShader = std::make_unique<ShaderLibrary>(result.pBlob, defines);
	m_Libraries.push_back(std::move(pNewShader));
	ShaderLibrary* pLibrary = m_Libraries.back().get();

	for (const ShaderStringHash& include : includes)
	{
		m_IncludeDependencyMap[include].insert(pShaderPath);
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

bool ShaderManager::ProcessSource(const std::string& sourcePath, const std::string& filePath, std::stringstream& output, std::vector<ShaderStringHash>& processedIncludes)
{
	std::string line;

	int linesProcessed = 0;
	bool placedLineDirective = false;

	std::ifstream fileStream(filePath, std::ios::binary);

	if (fileStream.fail())
	{
		E_LOG(Error, "Failed to open file '%s'", filePath.c_str());
		return false;
	}

	while (getline(fileStream, line))
	{
		size_t includeStart = line.find("#include");
		if (includeStart != std::string::npos)
		{
			size_t start = line.find('"') + 1;
			size_t end = line.rfind('"');
			if (end == std::string::npos || start == std::string::npos || start == end)
			{
				E_LOG(Error, "Include syntax errror: %s", line.c_str());
				return false;
			}
			std::string includeFilePath = line.substr(start, end - start);
			ShaderStringHash includeHash(includeFilePath);
			if (std::find(processedIncludes.begin(), processedIncludes.end(), includeHash) == processedIncludes.end())
			{
				processedIncludes.push_back(includeHash);
				std::string basePath = Paths::GetDirectoryPath(filePath);
				std::string fullFilePath = basePath + includeFilePath;

				if (!ProcessSource(sourcePath, fullFilePath, output, processedIncludes))
				{
					return false;
				}
			}
			placedLineDirective = false;
		}
		else
		{
			if (placedLineDirective == false)
			{
				placedLineDirective = true;
#if USE_SHADER_LINE_DIRECTIVE
				output << "#line " << linesProcessed + 1 << " \"" << filePath << "\"\n";
#endif
			}
			output << line << '\n';
		}
		++linesProcessed;
	}
	return true;
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
