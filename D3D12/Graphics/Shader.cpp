#include "stdafx.h"
#include "Shader.h"
#include "Core/Paths.h"
#include <dxcapi.h>

#ifndef USE_SHADER_LINE_DIRECTIVE
#define USE_SHADER_LINE_DIRECTIVE 1
#endif

std::vector<std::pair<std::string, std::string>> Shader::m_GlobalShaderDefines;

Shader::Shader(const char* pFilePath, Type shaderType, const char* pEntryPoint, const std::vector<std::string> defines)
{
	m_Path = pFilePath;
	m_Type = shaderType;
	Compile(pFilePath, shaderType, pEntryPoint, 6, 0, defines);
}

Shader::~Shader()
{

}

bool Shader::ProcessSource(const std::string& sourcePath, const std::string& filePath, std::stringstream& output, std::vector<StringHash>& processedIncludes, std::vector<std::string>& dependencies)
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
				output << "#line " << linesProcessed + 1 << " \"" << filePath << "\"\n";
#endif
			}
			output << line << '\n';
		}
		++linesProcessed;
	}
	return true;
}

bool Shader::Compile(const char* pFilePath, Type shaderType, const char* pEntryPoint, char shaderModelMajor, char shaderModelMinor, const std::vector<std::string> defines /*= {}*/)
{
	std::stringstream shaderSource;
	std::vector<StringHash> includes;
	if (!Shader::ProcessSource(pFilePath, pFilePath, shaderSource, includes, m_Dependencies))
	{
		return false;
	}
	std::string source = shaderSource.str();
	std::string target = GetShaderTarget(shaderType, shaderModelMajor, shaderModelMinor);

	if (shaderModelMajor < 6)
	{
		return CompileFxc(source, target.c_str(), pEntryPoint, defines);
	}
	return CompileDxc(source, target.c_str(), pEntryPoint, defines);
}

bool Shader::CompileDxc(const std::string& source, const char* pTarget, const char* pEntryPoint, const std::vector<std::string> defines /*= {}*/)
{
	static ComPtr<IDxcLibrary> pLibrary;
	static ComPtr<IDxcCompiler> pCompiler;
	if (!pCompiler)
	{
		HR(DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(pLibrary.GetAddressOf())));
		HR(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(pCompiler.GetAddressOf())));
	}

	ComPtr<IDxcBlobEncoding> pSource;
	HR(pLibrary->CreateBlobWithEncodingFromPinned(source.c_str(), (uint32)source.size(), CP_UTF8, pSource.GetAddressOf()));

	wchar_t target[256];
	ToWidechar(pTarget, target, 256);

	const LPCWSTR* pCompileArguments;
	int numCompileArguments = 0;
	const constexpr LPCWSTR pArgs[] =
	{
		L"/Zpr",
		L"/WX",
		L"/O3",
	};
	const constexpr LPCWSTR pDebugArgs[] =
	{
		L"/Zpr",
		L"/WX",
		L"/Zi",
		L"/Qembed_debug",
		L"/Od",
	};

	bool debugShaders = CommandLine::GetBool("DebugShaders");
	pCompileArguments = debugShaders ? &pDebugArgs[0] : &pArgs[0];
	numCompileArguments = debugShaders ? ARRAYSIZE(pDebugArgs) : ARRAYSIZE(pArgs);
	
	std::vector<std::wstring> dDefineNames;
	std::vector<std::wstring> dDefineValues;
	for (const std::string& define : defines)
	{
		dDefineNames.push_back(std::wstring(define.begin(), define.end()));
		dDefineValues.push_back(L"1");
	}
	for (const auto& define : m_GlobalShaderDefines)
	{
		dDefineNames.push_back(std::wstring(define.first.begin(), define.first.end()));
		dDefineValues.push_back(std::wstring(define.second.begin(), define.second.end()));
	}

	std::vector<DxcDefine> dxcDefines;
	for (size_t i = 0; i < dDefineNames.size(); ++i)
	{
		DxcDefine m;
		m.Name = dDefineNames[i].c_str();
		m.Value = dDefineValues[i].c_str();
		dxcDefines.push_back(m);
	}

	wchar_t fileName[256], entryPoint[256];
	ToWidechar(m_Path.c_str(), fileName, 256);
	ToWidechar(pEntryPoint, entryPoint, 256);

	ComPtr<IDxcOperationResult> pCompileResult;

	HR(pCompiler->Compile(pSource.Get(), fileName, entryPoint, target, const_cast<LPCWSTR*>(pCompileArguments), numCompileArguments, dxcDefines.data(), (uint32)dxcDefines.size(), nullptr, pCompileResult.GetAddressOf()));

	HRESULT hrCompilation;
	HR(pCompileResult->GetStatus(&hrCompilation));

	if (hrCompilation < 0)
	{
		ComPtr<IDxcBlobEncoding> pPrintBlob, pPrintBlob8;
		HR(pCompileResult->GetErrorBuffer(pPrintBlob.GetAddressOf()));
		pLibrary->GetBlobAsUtf8(pPrintBlob.Get(), pPrintBlob8.GetAddressOf());
		E_LOG(Error, "%s", (char*)pPrintBlob8->GetBufferPointer());
		return false;
	}

	IDxcBlob** pBlob = reinterpret_cast<IDxcBlob**>(m_pByteCode.GetAddressOf());
	pCompileResult->GetResult(pBlob);

	return true;
}

bool Shader::CompileFxc(const std::string& source, const char* pTarget, const char* pEntryPoint, const std::vector<std::string> defines /*= {}*/)
{
	uint32 compileFlags = D3DCOMPILE_PACK_MATRIX_ROW_MAJOR;
#if defined(_DEBUG)
	// Enable better shader debugging with the graphics debugging tools.
	compileFlags |= D3DCOMPILE_DEBUG;
	compileFlags |= D3DCOMPILE_SKIP_OPTIMIZATION;
	compileFlags |= D3DCOMPILE_PREFER_FLOW_CONTROL;
#else
	compileFlags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

	std::vector<D3D_SHADER_MACRO> shaderDefines;
	for (const std::string& define : defines)
	{
		D3D_SHADER_MACRO m;
		m.Name = define.c_str();
		m.Definition = "1";
		shaderDefines.push_back(m);
	}

	for (const auto& define : m_GlobalShaderDefines)
	{
		D3D_SHADER_MACRO m;
		m.Name = define.first.c_str();
		m.Definition = define.second.c_str();
		shaderDefines.push_back(m);
	}

	D3D_SHADER_MACRO endMacro;
	endMacro.Name = nullptr;
	endMacro.Definition = nullptr;
	shaderDefines.push_back(endMacro);

	std::string filePath = m_Path;

	ComPtr<ID3DBlob> pErrorBlob;
	D3DCompile(source.data(), source.size(), filePath.c_str(), shaderDefines.data(), nullptr, pEntryPoint, pTarget, compileFlags, 0, m_pByteCode.GetAddressOf(), pErrorBlob.GetAddressOf());
	if (pErrorBlob != nullptr)
	{
		std::wstring errorMsg = std::wstring((char*)pErrorBlob->GetBufferPointer(), (char*)pErrorBlob->GetBufferPointer() + pErrorBlob->GetBufferSize());
		std::wcout << errorMsg << std::endl;
		return false;
	}
	pErrorBlob.Reset();
	return true;
}


std::string Shader::GetShaderTarget(Type shaderType, char shaderModelMajor, char shaderModelMinor)
{
	char out[7];
	switch (shaderType)
	{
	case Type::VertexShader:
		sprintf_s(out, "vs_%d_%d", shaderModelMajor, shaderModelMinor);
		break;
	case Type::PixelShader:
		sprintf_s(out, "ps_%d_%d", shaderModelMajor, shaderModelMinor);
		break;
	case Type::GeometryShader:
		sprintf_s(out, "gs_%d_%d", shaderModelMajor, shaderModelMinor);
		break;
	case Type::ComputeShader:
		sprintf_s(out, "cs_%d_%d", shaderModelMajor, shaderModelMinor);
		break;
	case Type::MAX:
	default:
		sprintf_s(out, "");
	}
	return out;
}

void* Shader::GetByteCode() const
{
	return m_pByteCode->GetBufferPointer();
}

uint32 Shader::GetByteCodeSize() const
{
	return (uint32)m_pByteCode->GetBufferSize();
}

void Shader::AddGlobalShaderDefine(const std::string& name, const std::string& value /*= "1"*/)
{
	m_GlobalShaderDefines.emplace_back(name, value);
}

ShaderLibrary::ShaderLibrary(const char* pFilePath, const std::vector<std::string> defines)
{
	m_Path = pFilePath;

	std::stringstream shaderSource;
	std::vector<StringHash> includes;
	if (!Shader::ProcessSource(pFilePath, pFilePath, shaderSource, includes, m_Dependencies))
	{
		return;
	}
	std::string source = shaderSource.str();

	ComPtr<IDxcLibrary> pLibrary;
	HR(DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(pLibrary.GetAddressOf())));
	ComPtr<IDxcCompiler> pCompiler;
	HR(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(pCompiler.GetAddressOf())));

	ComPtr<IDxcBlobEncoding> pSource;
	HR(pLibrary->CreateBlobWithEncodingFromPinned(source.c_str(), (uint32)source.size(), CP_UTF8, pSource.GetAddressOf()));

	std::vector<std::wstring> dDefineNames;
	std::vector<std::wstring> dDefineValues;
	for (const std::string& define : defines)
	{
		dDefineNames.push_back(std::wstring(define.begin(), define.end()));
		dDefineValues.push_back(L"1");
	}

	std::vector<DxcDefine> dxcDefines;
	for (size_t i = 0; i < dDefineNames.size(); ++i)
	{
		DxcDefine m;
		m.Name = dDefineNames[i].c_str();
		m.Value = dDefineValues[i].c_str();
		dxcDefines.push_back(m);
	}

	wchar_t fileName[256];
	ToWidechar(m_Path.c_str(), fileName, 256);

	static const constexpr LPCWSTR pArgs[] =
	{
		L"/Zpr",
		L"/WX",
		L"/O3",
	};

	ComPtr<IDxcOperationResult> pCompileResult;

	HR(pCompiler->Compile(pSource.Get(), fileName, L"", L"lib_6_3", const_cast<LPCWSTR*>(pArgs), ARRAYSIZE(pArgs), dxcDefines.data(), (uint32)dxcDefines.size(), nullptr, pCompileResult.GetAddressOf()));

	auto checkResult = [&](IDxcOperationResult* pResult) {
		HRESULT hrCompilation;
		HR(pResult->GetStatus(&hrCompilation));

		if (hrCompilation < 0)
		{
			ComPtr<IDxcBlobEncoding> pPrintBlob, pPrintBlob8;
			HR(pResult->GetErrorBuffer(pPrintBlob.GetAddressOf()));
			pLibrary->GetBlobAsUtf8(pPrintBlob.Get(), pPrintBlob8.GetAddressOf());
			E_LOG(Error, "%s", (char*)pPrintBlob8->GetBufferPointer());
			return false;
		}
		return true;
	};

	if (!checkResult(pCompileResult.Get()))
	{
		return;
	}

	IDxcBlob** pBlob = reinterpret_cast<IDxcBlob**>(m_pByteCode.GetAddressOf());
	pCompileResult->GetResult(pBlob);
}
