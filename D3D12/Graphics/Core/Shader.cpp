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

	CompileResult CompileDxc(const char* pIdentifier, const char* pShaderSource, uint32 shaderSourceSize, const char* pEntryPoint, const char* pTarget, const std::vector<std::string>& defines)
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
		VERIFY_HR(pUtils->CreateBlob(pShaderSource, shaderSourceSize, CP_UTF8, pSource.GetAddressOf()));

		bool debugShaders = CommandLine::GetBool("debugshaders");

		std::vector<std::wstring> wDefines;
		for (const std::string& define : defines)
		{
			wDefines.push_back(MULTIBYTE_TO_UNICODE(define.c_str()));
		}

		std::vector<LPCWSTR> arguments;
		arguments.reserve(20);

		arguments.push_back(L"-E");

		MultibyteToUnicode pwEntry(pEntryPoint);
		arguments.push_back(*pwEntry);

		arguments.push_back(L"-T");
		MultibyteToUnicode pwTarget(pTarget);
		arguments.push_back(*pwTarget);
		arguments.push_back(L"-all_resources_bound");

		MultibyteToUnicode pwSymbolPath(pShaderSymbolsPath);
		if (debugShaders)
		{
			arguments.push_back(DXC_ARG_SKIP_OPTIMIZATIONS);
			arguments.push_back(L"-Qembed_debug");
		}
		else
		{
			arguments.push_back(DXC_ARG_OPTIMIZATION_LEVEL3);
			arguments.push_back(L"-Qstrip_debug");
			arguments.push_back(L"/Fd");
			arguments.push_back(*pwSymbolPath);
			arguments.push_back(L"-Qstrip_reflect");
		}

		arguments.push_back(DXC_ARG_WARNINGS_ARE_ERRORS);
		arguments.push_back(DXC_ARG_DEBUG);
		arguments.push_back(DXC_ARG_PACK_MATRIX_ROW_MAJOR);

		for (size_t i = 0; i < wDefines.size(); ++i)
		{
			arguments.push_back(L"-D");
			arguments.push_back(wDefines[i].c_str());
		}

		DxcBuffer sourceBuffer;
		sourceBuffer.Ptr = pSource->GetBufferPointer();
		sourceBuffer.Size = pSource->GetBufferSize();
		sourceBuffer.Encoding = 0;

		ComPtr<IDxcResult> pCompileResult;
		VERIFY_HR(pCompiler->Compile(&sourceBuffer, arguments.data(), (uint32)arguments.size(), nullptr, IID_PPV_ARGS(pCompileResult.GetAddressOf())));

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
			pCompileResult->GetOutput(DXC_OUT_PDB, IID_PPV_ARGS(result.pSymbolsBlob.GetAddressOf()), pDebugDataPath.GetAddressOf());
			std::stringstream pathStream;
			pathStream << pShaderSymbolsPath << UNICODE_TO_MULTIBYTE((wchar_t*)pDebugDataPath->GetBufferPointer());
			result.DebugPath = pathStream.str();
		}

		//Reflection
		{
			ComPtr<IDxcBlob> pReflectionData;
			pCompileResult->GetOutput(DXC_OUT_REFLECTION, IID_PPV_ARGS(pReflectionData.GetAddressOf()), nullptr);
			DxcBuffer reflectionBuffer;
			reflectionBuffer.Ptr = pReflectionData->GetBufferPointer();
			reflectionBuffer.Size = pReflectionData->GetBufferSize();
			reflectionBuffer.Encoding = 0;
			VERIFY_HR(pUtils->CreateReflection(&reflectionBuffer, IID_PPV_ARGS(result.pReflection.GetAddressOf())));
		}

		return result;
	}

	CompileResult CompileFxc(const char* pIdentifier, const char* pShaderSource, uint32 shaderSourceSize, const char* pEntryPoint, const char* pTarget, const std::vector<std::string>& defines)
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
		D3DCompile(pShaderSource, shaderSourceSize, pIdentifier, shaderDefines.data(), nullptr, pEntryPoint, pTarget, compileFlags, 0, (ID3DBlob**)result.pBlob.GetAddressOf(), pErrorBlob.GetAddressOf());
		if (pErrorBlob != nullptr)
		{
			result.ErrorMessage = (char*)pErrorBlob->GetBufferPointer();
			result.Success = false;
		}
		else
		{
			result.Success = true;
		}
		return result;
	}

	CompileResult Compile(const char* pIdentifier, const char* pShaderSource, uint32 shaderSourceSize, const char* pTarget, const char* pEntryPoint, uint32 majVersion, uint32 minVersion, const std::vector<ShaderDefine>& defines /*= {}*/)
	{
		char target[16];
		size_t i = strlen(pTarget);
		memcpy(target, pTarget, i);
		target[i++] = '_';
		target[i++] = '0' + (char)majVersion;
		target[i++] = '_';
		target[i++] = '0' + (char)minVersion;
		target[i++] = 0;

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

		std::stringstream str;
		str << "_SM_MAJ=" << majVersion;
		definesActual.push_back(str.str());
		str = std::stringstream();
		str << "_SM_MIN=" << minVersion;
		definesActual.push_back(str.str());

		if (majVersion < 6)
		{
			definesActual.emplace_back("_FXC=1");
			return CompileFxc(pIdentifier, pShaderSource, shaderSourceSize, pEntryPoint, target, definesActual);
		}
		definesActual.emplace_back("_DXC=1");
		return CompileDxc(pIdentifier, pShaderSource, shaderSourceSize, pEntryPoint, target, definesActual);
	}
}

ShaderBase::~ShaderBase()
{

}

ShaderManager::ShaderStringHash ShaderManager::GetEntryPointHash(const std::string& entryPoint, const std::vector<ShaderDefine>& defines)
{
	StringHash hash(entryPoint);
	for (const ShaderDefine& define : defines)
	{
		hash.Combine(StringHash(define.Value));
	}
	return hash;
}

Shader* ShaderManager::LoadShader(const std::string& shaderPath, ShaderType shaderType, const std::string& entryPoint, const std::vector<ShaderDefine>& defines /*= {}*/)
{
	std::stringstream shaderSource;
	std::vector<ShaderStringHash> includes;
	std::string filePath = m_ShaderSourcePath + shaderPath;
	if (!ProcessSource(filePath, filePath, shaderSource, includes))
	{
		return nullptr;
	}

	std::string source = shaderSource.str();
	ShaderCompiler::CompileResult result = ShaderCompiler::Compile(
		shaderPath.c_str(),
		source.c_str(),
		(uint32)source.size(),
		ShaderCompiler::GetShaderTarget(shaderType),
		entryPoint.c_str(),
		m_ShaderModelMajor,
		m_ShaderModelMinor, defines);

	if (!result.Success)
	{
		E_LOG(Warning, "Failed to compile shader \"%s\": %s", shaderPath.c_str(), result.ErrorMessage.c_str());
		return nullptr;
	}

	ShaderPtr pNewShader = std::make_unique<Shader>(result.pBlob, shaderType, entryPoint, defines);
	m_Shaders.push_back(std::move(pNewShader));
	Shader* pShader = m_Shaders.back().get();

	for (const ShaderStringHash& include : includes)
	{
		m_IncludeDependencyMap[include].insert(shaderPath);
	}
	m_IncludeDependencyMap[ShaderStringHash(shaderPath)].insert(shaderPath);

	StringHash hash = GetEntryPointHash(entryPoint, defines);
	m_FilepathToObjectMap[ShaderStringHash(shaderPath)].Shaders[hash] = pShader;

	return pShader;
}

ShaderLibrary* ShaderManager::LoadShaderLibrary(const std::string& shaderPath, const std::vector<ShaderDefine>& defines /*= {}*/)
{
	std::stringstream shaderSource;
	std::vector<ShaderStringHash> includes;
	std::string filePath = m_ShaderSourcePath + shaderPath;
	if (!ProcessSource(filePath, filePath, shaderSource, includes))
	{
		return nullptr;
	}

	std::string source = shaderSource.str();
	ShaderCompiler::CompileResult result = ShaderCompiler::Compile(
		shaderPath.c_str(),
		source.c_str(),
		(uint32)source.size(),
		"lib",
		"",
		m_ShaderModelMajor,
		m_ShaderModelMinor, defines);

	if (!result.Success)
	{
		E_LOG(Warning, "Failed to compile library \"%s\": %s", shaderPath.c_str(), result.ErrorMessage.c_str());
		return nullptr;
	}

	LibraryPtr pNewShader = std::make_unique<ShaderLibrary>(result.pBlob, defines);
	m_Libraries.push_back(std::move(pNewShader));
	ShaderLibrary* pLibrary = m_Libraries.back().get();

	for (const ShaderStringHash& include : includes)
	{
		m_IncludeDependencyMap[include].insert(shaderPath);
	}
	m_IncludeDependencyMap[ShaderStringHash(shaderPath)].insert(shaderPath);

	StringHash hash = GetEntryPointHash("", defines);
	m_FilepathToObjectMap[ShaderStringHash(shaderPath)].Libraries[hash] = pLibrary;

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
					Shader* pNewShader = LoadShader(dependency, pOldShader->GetType(), pOldShader->GetEntryPoint().c_str(), pOldShader->GetDefines());
					if (pNewShader)
					{
						E_LOG(Info, "Reloaded shader: \"%s - %s\"", dependency.c_str(), pNewShader->GetEntryPoint().c_str());
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
					ShaderLibrary* pNewLibrary = LoadShaderLibrary(dependency, pOldLibrary->GetDefines());
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

ShaderManager::ShaderManager(const std::string& shaderSourcePath, uint8 shaderModelMaj, uint8 shaderModelMin)
	: m_ShaderSourcePath(shaderSourcePath), m_ShaderModelMajor(shaderModelMaj), m_ShaderModelMinor(shaderModelMin)
{
	if (CommandLine::GetBool("shaderhotreload"))
	{
		m_pFileWatcher = std::make_unique<FileWatcher>();
		m_pFileWatcher->StartWatching(shaderSourcePath, true);
		E_LOG(Info, "Shader Hot-Reload enabled: \"%s\"", shaderSourcePath.c_str());
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
			}
		}
	}
}

Shader* ShaderManager::GetShader(const std::string& shaderPath, ShaderType shaderType, const std::string& entryPoint, const std::vector<ShaderDefine>& defines /*= {}*/)
{
	auto& shaderMap = m_FilepathToObjectMap[ShaderStringHash(shaderPath)].Shaders;
	StringHash hash = GetEntryPointHash(entryPoint, defines);
	auto it = shaderMap.find(hash);
	if (it != shaderMap.end())
	{
		return it->second;
	}

	return LoadShader(shaderPath, shaderType, entryPoint, defines);
}

ShaderLibrary* ShaderManager::GetLibrary(const std::string& shaderPath, const std::vector<ShaderDefine>& defines /*= {}*/)
{
	auto& libraryMap = m_FilepathToObjectMap[ShaderStringHash(shaderPath)].Libraries;
	StringHash hash = GetEntryPointHash("", defines);
	auto it = libraryMap.find(hash);
	if (it != libraryMap.end())
	{
		return it->second;
	}

	return LoadShaderLibrary(shaderPath, defines);
}
