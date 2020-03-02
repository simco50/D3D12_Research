#include "stdafx.h"
#include "Shader.h"
#include <fstream>
#include "Core/Paths.h"
#include <dxcapi.h>

#define USE_SHADER_LINE_DIRECTIVE 1


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

bool Shader::ProcessSource(const std::string& filePath, std::stringstream& output, std::vector<StringHash>& processedIncludes, std::vector<std::string>& dependencies)
{
	if (m_Path != filePath)
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

				if (!ProcessSource(filePath, output, processedIncludes, dependencies))
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
	std::vector<std::string> dependencies;
	if (!ProcessSource(pFilePath, shaderSource, includes, dependencies))
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
	IDxcLibrary* pLibrary;
	DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&pLibrary));
	IDxcCompiler* pCompiler;
	DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&pCompiler));

	IDxcBlobEncoding* pSource;
	HR(pLibrary->CreateBlobWithEncodingFromPinned(source.c_str(), (uint32)source.size(), CP_UTF8, &pSource));

	size_t written = 0;
	wchar_t target[256];
	mbstowcs_s(&written, target, pTarget, 256);

	LPCWSTR pArgs[] =
	{
		L"/Zpr",
		L"/WX",
#ifdef _DEBUG
		L"/Zi",
		L"/Qembed_debug",
		L"/Od",
#else
		L"/O3",
#endif
	};
	
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
	mbstowcs_s(&written, fileName, m_Path.c_str(), 256);
	mbstowcs_s(&written, entryPoint, pEntryPoint, 256);
	IDxcOperationResult* pCompileResult;

	HR(pCompiler->Compile(pSource, fileName, entryPoint, target, &pArgs[0], sizeof(pArgs) / sizeof(pArgs[0]), dxcDefines.data(), (uint32)dxcDefines.size(), nullptr, &pCompileResult));

	HRESULT hrCompilation;
	pCompileResult->GetStatus(&hrCompilation);

	if (hrCompilation < 0)
	{
		IDxcBlobEncoding* pPrintBlob, * pPrintBlob8;
		HR(pCompileResult->GetErrorBuffer(&pPrintBlob));
		pLibrary->GetBlobAsUtf8(pPrintBlob, &pPrintBlob8);
		E_LOG(Error, "%s", (char*)pPrintBlob8->GetBufferPointer());
		pPrintBlob->Release();
		pPrintBlob8->Release();
		return false;
	}

	IDxcBlob** pBlob = reinterpret_cast<IDxcBlob**>(m_pByteCode.GetAddressOf());
	pCompileResult->GetResult(pBlob);
	// Save to a file, disassemble and print, store somewhere ...
	pCompileResult->Release();

	ComPtr<IDxcValidator> pValidator;
	DxcCreateInstance(CLSID_DxcValidator, IID_PPV_ARGS(pValidator.GetAddressOf()));
	pValidator->Validate(*pBlob, DxcValidatorFlags_InPlaceEdit, &pCompileResult);
	pCompileResult->GetStatus(&hrCompilation);
	if (hrCompilation < 0)
	{
		IDxcBlobEncoding* pPrintBlob, * pPrintBlob8;
		HR(pCompileResult->GetErrorBuffer(&pPrintBlob));
		pLibrary->GetBlobAsUtf8(pPrintBlob, &pPrintBlob8);
		E_LOG(Error, "%s", (char*)pPrintBlob8->GetBufferPointer());
		pPrintBlob->Release();
		pPrintBlob8->Release();
		return false;
	}

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
