#include "stdafx.h"
#include "Shader.h"
#include <fstream>

bool Shader::Load(const char* pFilePath, Type shaderType, const char* pEntryPoint)
{
#if defined(_DEBUG)
	// Enable better shader debugging with the graphics debugging tools.
	uint32 compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
	uint32 compileFlags = 0;
#endif
	compileFlags |= D3DCOMPILE_PACK_MATRIX_ROW_MAJOR;

	std::ifstream file(pFilePath, std::ios::ate);
	if (file.fail())
	{
		return false;
	}
	int size = (int)file.tellg();
	std::vector<char> data(size);
	file.seekg(0);
	file.read(data.data(), data.size());
	file.close();

	std::string shaderModel = "";
	switch (shaderType)
	{
	case Type::VertexShader:
		shaderModel = "vs_5_0";
		break;
	case Type::PixelShader:
		shaderModel = "ps_5_0";
		break;
	case Type::MAX:
	default:
		return false;
	}
	m_Type = shaderType;

	ComPtr<ID3DBlob> pErrorBlob;
	D3DCompile2(data.data(), data.size(), nullptr, nullptr, nullptr, pEntryPoint, shaderModel.c_str(), compileFlags, 0, 0, nullptr, 0, m_pByteCode.GetAddressOf(), pErrorBlob.GetAddressOf());
	if (pErrorBlob != nullptr)
	{
		std::wstring errorMsg = std::wstring((char*)pErrorBlob->GetBufferPointer(), (char*)pErrorBlob->GetBufferPointer() + pErrorBlob->GetBufferSize());
		std::wcout << errorMsg << std::endl;
		return false;
	}
	pErrorBlob.Reset();
	return true;
}
