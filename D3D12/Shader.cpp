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

	ShaderReflection();

	return true;
}

void Shader::ShaderReflection()
{
	ComPtr<ID3D12ShaderReflection> pShaderReflection;
	D3D12_SHADER_DESC shaderDesc;

	HR(D3DReflect(m_pByteCode->GetBufferPointer(), m_pByteCode->GetBufferSize(), IID_PPV_ARGS(pShaderReflection.GetAddressOf())));
	pShaderReflection->GetDesc(&shaderDesc);

	std::map<std::string, uint32> cbRegisterMap;

	for (unsigned i = 0; i < shaderDesc.BoundResources; ++i)
	{
		D3D12_SHADER_INPUT_BIND_DESC resourceDesc;
		pShaderReflection->GetResourceBindingDesc(i, &resourceDesc);

		switch (resourceDesc.Type)
		{
		case D3D_SIT_CBUFFER:
		case D3D_SIT_TBUFFER:
			cbRegisterMap[resourceDesc.Name] = resourceDesc.BindPoint;
			break;
		case D3D_SIT_TEXTURE:
		case D3D_SIT_SAMPLER:
		case D3D_SIT_UAV_RWTYPED:
		case D3D_SIT_STRUCTURED:
		case D3D_SIT_UAV_RWSTRUCTURED:
		case D3D_SIT_BYTEADDRESS:
		case D3D_SIT_UAV_RWBYTEADDRESS:
		case D3D_SIT_UAV_APPEND_STRUCTURED:
		case D3D_SIT_UAV_CONSUME_STRUCTURED:
		case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
		default:
			break;
		}
	}

	for (unsigned int c = 0; c < shaderDesc.ConstantBuffers; ++c)
	{
		ID3D12ShaderReflectionConstantBuffer* pReflectionConstantBuffer = pShaderReflection->GetConstantBufferByIndex(c);
		D3D12_SHADER_BUFFER_DESC bufferDesc;
		pReflectionConstantBuffer->GetDesc(&bufferDesc);
		uint32 cbRegister = cbRegisterMap[std::string(bufferDesc.Name)];

		for (unsigned v = 0; v < bufferDesc.Variables; ++v)
		{
			ID3D12ShaderReflectionVariable* pVariable = pReflectionConstantBuffer->GetVariableByIndex(v);
			D3D12_SHADER_VARIABLE_DESC variableDesc;
			pVariable->GetDesc(&variableDesc);

			ShaderParameter parameter = {};
			parameter.Name = variableDesc.Name;
			parameter.Offset = variableDesc.StartOffset;
			parameter.Size = variableDesc.Size;
			m_Parameters[variableDesc.Name] = parameter;
		}
	}
}
