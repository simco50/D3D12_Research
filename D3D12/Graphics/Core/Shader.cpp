#include "stdafx.h"
#include "Shader.h"
#include "Core/Paths.h"
#include "Core/CommandLine.h"

#ifndef USE_SHADER_LINE_DIRECTIVE
#define USE_SHADER_LINE_DIRECTIVE 1
#endif

namespace ShaderCompiler
{
	constexpr const char* pShaderSymbolsPath = "_Temp/ShaderSymbols/";

	constexpr const char* GetShaderTarget(ShaderType t)
	{
		switch (t)
		{
		case ShaderType::Vertex:	return "vs";
		case ShaderType::Pixel:		return "ps";
		case ShaderType::Geometry:	return "gs";
		case ShaderType::Compute:	return "cs";
		case ShaderType::MAX:		return "lib"; //Assume MAX is shader lib
		default: noEntry();			return "";
		}
	}

	bool CompileDxc(const char* pIdentifier, const char* pShaderSource, uint32 shaderSourceSize, IDxcBlob** pOutput, const char* pEntryPoint /*= ""*/, const char* pTarget /*= ""*/, const std::vector<std::string>& defines /*= {}*/)
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

		wchar_t target[256], fileName[256], entryPoint[256];
		ToWidechar(pIdentifier, fileName, 256);
		ToWidechar(pEntryPoint, entryPoint, 256);
		ToWidechar(pTarget, target, 256);

		bool debugShaders = CommandLine::GetBool("debugshaders");

		std::vector<std::wstring> wDefines;
		for (const std::string& define : defines)
		{
			std::wstringstream str;
			wchar_t intermediate[256];
			ToWidechar(define.c_str(), intermediate, 256);
			str << intermediate << "=1";
			wDefines.push_back(str.str());
		}
		wDefines.push_back(L"_DXC=1");

		std::vector<LPCWSTR> arguments;
		arguments.reserve(20);

		arguments.push_back(L"-E");
		arguments.push_back(entryPoint);

		arguments.push_back(L"-T");
		arguments.push_back(target);

		if (debugShaders)
		{
			arguments.push_back(DXC_ARG_SKIP_OPTIMIZATIONS);
			arguments.push_back(L"-Qembed_debug");
		}
		else
		{
			arguments.push_back(DXC_ARG_OPTIMIZATION_LEVEL3);
			arguments.push_back(L"-Qstrip_debug");
			wchar_t symbolsPath[256];
			ToWidechar(pShaderSymbolsPath, symbolsPath, 256);
			arguments.push_back(L"/Fd");
			arguments.push_back(symbolsPath);
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

		ComPtr<IDxcBlobUtf8> pErrors;
		pCompileResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(pErrors.GetAddressOf()), nullptr);
		if (pErrors && pErrors->GetStringLength() > 0)
		{
			E_LOG(Warning, "%s", (char*)pErrors->GetBufferPointer());
			return false;
		}

		//Shader object
		{
			VERIFY_HR(pCompileResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(pOutput), nullptr));
		}

		//Validation
		{
			ComPtr<IDxcOperationResult> pResult;
			VERIFY_HR(pValidator->Validate(*pOutput, DxcValidatorFlags_InPlaceEdit, pResult.GetAddressOf()));
			HRESULT validationResult;
			pResult->GetStatus(&validationResult);
			if (validationResult != S_OK)
			{
				ComPtr<IDxcBlobEncoding> pPrintBlob;
				ComPtr<IDxcBlobUtf8> pPrintBlobUtf8;
				pResult->GetErrorBuffer(pPrintBlob.GetAddressOf());
				pUtils->GetBlobAsUtf8(pPrintBlob.Get(), pPrintBlobUtf8.GetAddressOf());
				E_LOG(Warning, "%s", pPrintBlobUtf8->GetBufferPointer());
			}
		}

#if 0
		//Symbols
		{
			ComPtr<IDxcBlob> pDebugData;
			ComPtr<IDxcBlobUtf16> pDebugDataPath;
			pCompileResult->GetOutput(DXC_OUT_PDB, IID_PPV_ARGS(pDebugData.GetAddressOf()), pDebugDataPath.GetAddressOf());
			Paths::CreateDirectoryTree(pShaderSymbolsPath);
			std::stringstream pathStream;
			char path[256];
			ToMultibyte((wchar_t*)pDebugDataPath->GetBufferPointer(), path, 256);
			pathStream << pShaderSymbolsPath << path;
			std::ofstream fileStream(pathStream.str(), std::ios::binary);
			fileStream.write((char*)pDebugData->GetBufferPointer(), pDebugData->GetBufferSize());
		}
#endif
#if 0
		//Reflection
		{
			ComPtr<IDxcBlob> pReflectionData;
			pCompileResult->GetOutput(DXC_OUT_REFLECTION, IID_PPV_ARGS(pReflectionData.GetAddressOf()), nullptr);
			DxcBuffer reflectionBuffer;
			reflectionBuffer.Ptr = pReflectionData->GetBufferPointer();
			reflectionBuffer.Size = pReflectionData->GetBufferSize();
			reflectionBuffer.Encoding = 0;
			if (strcmp(pEntryPoint, "") == 0)
			{
				ComPtr<ID3D12LibraryReflection> pLibraryReflection;
				VERIFY_HR(pUtils->CreateReflection(&reflectionBuffer, IID_PPV_ARGS(pLibraryReflection.GetAddressOf())));
				D3D12_LIBRARY_DESC libraryDesc;
				pLibraryReflection->GetDesc(&libraryDesc);
			}
			else
			{
				ComPtr<ID3D12ShaderReflection> pShaderReflection;
				VERIFY_HR(pUtils->CreateReflection(&reflectionBuffer, IID_PPV_ARGS(pShaderReflection.GetAddressOf())));
				D3D12_SHADER_DESC shaderDesc;
				pShaderReflection->GetDesc(&shaderDesc);
			}
		}
#endif
		return true;
	}

	bool CompileFxc(const char* pIdentifier, const char* pShaderSource, uint32 shaderSourceSize, ID3DBlob** pOutput, const char* pEntryPoint /*= ""*/, const char* pTarget /*= ""*/, const std::vector<std::string>& defines /*= {}*/)
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

		std::vector<std::string> globalDefines;
		globalDefines.push_back("_FXC=1");

		std::vector<D3D_SHADER_MACRO> shaderDefines;
		for (const std::string& define : defines)
		{
			D3D_SHADER_MACRO m;
			m.Name = define.c_str();
			m.Definition = "1";
			shaderDefines.push_back(m);
		}
		for (const std::string& define : globalDefines)
		{
			D3D_SHADER_MACRO m;
			m.Name = define.c_str();
			m.Definition = "1";
			shaderDefines.push_back(m);
		}

		D3D_SHADER_MACRO endMacro;
		endMacro.Name = nullptr;
		endMacro.Definition = nullptr;
		shaderDefines.push_back(endMacro);

		ComPtr<ID3DBlob> pErrorBlob;
		D3DCompile(pShaderSource, shaderSourceSize, pIdentifier, shaderDefines.data(), nullptr, pEntryPoint, pTarget, compileFlags, 0, pOutput, pErrorBlob.GetAddressOf());
		if (pErrorBlob != nullptr)
		{
			std::wstring errorMsg = std::wstring((char*)pErrorBlob->GetBufferPointer(), (char*)pErrorBlob->GetBufferPointer() + pErrorBlob->GetBufferSize());
			std::wcout << errorMsg << std::endl;
			return false;
		}
		pErrorBlob.Reset();
		return true;
	}

	bool Compile(const char* pIdentifier, const char* pShaderSource, uint32 shaderSourceSize, ShaderType type, IDxcBlob** pOutput, const char* pEntryPoint, uint32 majVersion, uint32 minVersion, const std::vector<std::string>& defines /*= {}*/)
	{
		char target[16];
		const char* pTargetType = GetShaderTarget(type);
		size_t i = strlen(pTargetType);
		memcpy(target, pTargetType, i);
		target[i++] = '_';
		target[i++] = '0' + majVersion;
		target[i++] = '_';
		target[i++] = '0' + minVersion;
		target[i++] = 0;

		if (majVersion < 6)
		{
			ID3DBlob** pBlob = reinterpret_cast<ID3DBlob**>(pOutput);
			return CompileFxc(pIdentifier, pShaderSource, shaderSourceSize, pBlob, pEntryPoint, target, defines);
		}
		return CompileDxc(pIdentifier, pShaderSource, shaderSourceSize, pOutput, pEntryPoint, target, defines);
	}
}

bool ShaderBase::ProcessSource(const std::string& sourcePath, const std::string& filePath, std::stringstream& output, std::vector<StringHash>& processedIncludes, std::vector<std::string>& dependencies)
{
	if (sourcePath != filePath)
	{
		dependencies.push_back(filePath);
	}

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
		size_t start = line.find("#include");
		if (start != std::string::npos)
		{
			size_t start = line.find('"') + 1;
			size_t end = line.rfind('"');
			if (end == std::string::npos || start == std::string::npos || start == end)
			{
				E_LOG(Error, "Include syntax errror: %s", line.c_str());
				return false;
			}
			std::string includeFilePath = line.substr(start, end - start);
			StringHash includeHash(includeFilePath);
			if (std::find(processedIncludes.begin(), processedIncludes.end(), includeHash) == processedIncludes.end())
			{
				processedIncludes.push_back(includeHash);
				std::string basePath = Paths::GetDirectoryPath(filePath);
				std::string filePath = basePath + includeFilePath;

				if (!ProcessSource(sourcePath, filePath, output, processedIncludes, dependencies))
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
				if (CommandLine::GetBool("debugshaders") == false)
				{
					output << "#line " << linesProcessed + 1 << " \"" << filePath << "\"\n";
				}
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

Shader::Shader(const char* pFilePath, ShaderType shaderType, const char* pEntryPoint, const std::vector<std::string> defines)
{
	m_Path = Paths::ShadersDir() + pFilePath;
	m_Type = shaderType;
	Compile(m_Path.c_str(), shaderType, pEntryPoint, 6, 3, defines);
}

bool Shader::Compile(const char* pFilePath, ShaderType shaderType, const char* pEntryPoint, uint32 shaderModelMajor, uint32 shaderModelMinor, const std::vector<std::string> defines /*= {}*/)
{
	std::stringstream shaderSource;
	std::vector<StringHash> includes;
	if (!ShaderBase::ProcessSource(pFilePath, pFilePath, shaderSource, includes, m_Dependencies))
	{
		return false;
	}

	std::string source = shaderSource.str();
	return ShaderCompiler::Compile(pFilePath, source.c_str(), (uint32)source.size(), shaderType, m_pByteCode.GetAddressOf(), pEntryPoint, shaderModelMajor, shaderModelMinor, defines);
}

ShaderLibrary::ShaderLibrary(const char* pFilePath, const std::vector<std::string> defines)
{
	m_Path = Paths::ShadersDir() + pFilePath;
	Compile(m_Path.c_str(), 6, 3, defines);
}

bool ShaderLibrary::Compile(const char* pFilePath, uint32 shaderModelMajor, uint32 shaderModelMinor, const std::vector<std::string> defines /*= {}*/)
{
	std::stringstream shaderSource;
	std::vector<StringHash> includes;
	if (!ProcessSource(pFilePath, pFilePath, shaderSource, includes, m_Dependencies))
	{
		return false;
	}

	std::string source = shaderSource.str();
	return ShaderCompiler::Compile(pFilePath, source.c_str(), (uint32)source.size(), ShaderType::MAX, m_pByteCode.GetAddressOf(), "", shaderModelMajor, shaderModelMinor, defines);
}
