#include "stdafx.h"
#include "Shader.h"
#include <fstream>

class D3DInclude : public ID3DInclude
{
public:
	D3DInclude(const std::string& basePath)
		: m_BasePath(basePath)
	{}

	STDOVERRIDEMETHODIMP Open(THIS_ D3D_INCLUDE_TYPE /*IncludeType*/, LPCSTR pFileName, LPCVOID /*pParentData*/, LPCVOID *ppData, UINT *pBytes)
	{
		std::string filePath = m_BasePath + std::string(pFileName);
		std::ifstream stream(filePath.c_str(), std::ios::binary | std::ios::ate);
		if (stream.fail())
		{
			return E_FAIL;
		}
		*pBytes = (UINT)stream.tellg();
		*ppData = new char[*pBytes];
		stream.seekg(0);
		stream.read((char*)*ppData, *pBytes);
		stream.close();
		return S_OK;
	}

	STDOVERRIDEMETHODIMP Close(THIS_ LPCVOID pData)
	{
		if (pData)
		{
			char* pBuffer = (char*)pData;
			delete[] pBuffer;
		}
		return S_OK;
	}
private:
	std::string m_BasePath;
};

bool Shader::Load(const char* pFilePath, Type shaderType, const char* pEntryPoint)
{
#if defined(_DEBUG)
	// Enable better shader debugging with the graphics debugging tools.
	uint32 compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
	uint32 compileFlags = 0;
#endif
	compileFlags |= D3DCOMPILE_PACK_MATRIX_ROW_MAJOR;

	std::ifstream file(pFilePath);
	if (file.fail())
	{
		return false;
	}
	std::stringstream stream;
	std::string line;
	while (getline(file, line))
	{
		stream << line << "\n";
	}
	line = stream.str();

	std::string shaderModel = "";
	switch (shaderType)
	{
	case Type::VertexShader:
		shaderModel = "vs_5_0";
		break;
	case Type::PixelShader:
		shaderModel = "ps_5_0";
		break;
	case Type::ComputeShader:
		shaderModel = "cs_5_0";
		break;
	case Type::MAX:
	default:
		return false;
	}
	m_Type = shaderType;

	std::string filePath = pFilePath;
	D3DInclude include(filePath.substr(0, filePath.rfind('/') + 1));

	ComPtr<ID3DBlob> pErrorBlob;
	D3DCompile2(line.data(), line.size(), nullptr, nullptr, &include, pEntryPoint, shaderModel.c_str(), compileFlags, 0, 0, nullptr, 0, m_pByteCode.GetAddressOf(), pErrorBlob.GetAddressOf());
	if (pErrorBlob != nullptr)
	{
		std::wstring errorMsg = std::wstring((char*)pErrorBlob->GetBufferPointer(), (char*)pErrorBlob->GetBufferPointer() + pErrorBlob->GetBufferSize());
		std::wcout << errorMsg << std::endl;
		return false;
	}
	pErrorBlob.Reset();

	return true;
}