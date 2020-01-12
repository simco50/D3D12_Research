#include "stdafx.h"
#include "Shader.h"
#include <fstream>
#include "Core/Paths.h"\

#define USE_SHADER_LINE_DIRECTIVE 1
#define DXC_COMPILER 1

#if DXC_COMPILER
#include <dxcapi.h>
#pragma comment(lib, "dxcompiler.lib")
#endif

std::vector<std::pair<std::string, std::string>> Shader::m_GlobalShaderDefines;


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

Shader::Shader(const char* pFilePath, Type shaderType, const char* pEntryPoint, const std::vector<std::string> defines)
{
	std::stringstream shaderSource;
	std::vector<StringHash> includes;
	std::vector<std::string> dependencies;
	if (!ProcessSource(pFilePath, shaderSource, includes, dependencies))
	{
		return;
	}
	std::string source = shaderSource.str();

#if DXC_COMPILER
	IDxcLibrary* pLibrary;
	DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&pLibrary));
	IDxcCompiler* pCompiler;
	DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&pCompiler));

	IDxcBlobEncoding* pSource;
	HR(pLibrary->CreateBlobWithEncodingFromPinned(source.c_str(), (uint32)source.size(), CP_UTF8, &pSource));

	std::wstring target = L"";
	switch (shaderType)
	{
	case Type::VertexShader:
		target = L"vs_6_0";
		break;
	case Type::PixelShader:
		target = L"ps_6_0";
		break;
	case Type::GeometryShader:
		target = L"gs_6_0";
		break;
	case Type::ComputeShader:
		target = L"cs_6_0";
		break;
	case Type::MAX:
	default:
		return;
	}

	LPCWSTR pArgs[] =
	{
		L"/Zpr",
#ifdef _DEBUG
		L"-Zi",
#else
		L"-0d"
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
	size_t written = 0;
	mbstowcs_s(&written, fileName, pFilePath, 256);
	mbstowcs_s(&written, entryPoint, pEntryPoint, 256);
	IDxcOperationResult* pCompileResult;

	HR(pCompiler->Compile(pSource, fileName, entryPoint, target.c_str(), &pArgs[0], sizeof(pArgs) / sizeof(pArgs[0]), dxcDefines.data(), (uint32)dxcDefines.size(), nullptr, &pCompileResult));

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
		return;
	}

	pCompileResult->GetResult(m_pByteCodeDxc.GetAddressOf());
	// Save to a file, disassemble and print, store somewhere ...
	pCompileResult->Release();

#else
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

	std::string shaderModel = "";
	switch (shaderType)
	{
	case Type::VertexShader:
		shaderModel = "vs_5_0";
		break;
	case Type::PixelShader:
		shaderModel = "ps_5_0";
		break;
	case Type::GeometryShader:
		shaderModel = "gs_5_0";
		break;
	case Type::ComputeShader:
		shaderModel = "cs_5_0";
		break;
	case Type::MAX:
	default:
		return;
	}
	m_Type = shaderType;

	std::string filePath = pFilePath;

	ComPtr<ID3DBlob> pErrorBlob;
	D3DCompile(source.data(), source.size(), filePath.c_str(), shaderDefines.data(), nullptr, pEntryPoint, shaderModel.c_str(), compileFlags, 0, m_pByteCode.GetAddressOf(), pErrorBlob.GetAddressOf());
	if (pErrorBlob != nullptr)
	{
		std::wstring errorMsg = std::wstring((char*)pErrorBlob->GetBufferPointer(), (char*)pErrorBlob->GetBufferPointer() + pErrorBlob->GetBufferSize());
		std::wcout << errorMsg << std::endl;
		return;
	}
	pErrorBlob.Reset();
#endif
}

Shader::~Shader()
{

}

void* Shader::GetByteCode() const
{
#if DXC_COMPILER
	return m_pByteCodeDxc->GetBufferPointer();
#else
	return m_pByteCode->GetBufferPointer();
#endif
}

uint32 Shader::GetByteCodeSize() const
{
#if DXC_COMPILER
	return (uint32)m_pByteCodeDxc->GetBufferSize();
#else
	return (uint32)m_pByteCode->GetBufferSize();
#endif
}

void Shader::AddGlobalShaderDefine(const std::string& name, const std::string& value /*= "1"*/)
{
	m_GlobalShaderDefines.emplace_back(name, value);
}
