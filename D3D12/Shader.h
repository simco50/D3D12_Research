#pragma once

struct ShaderParameter
{
	std::string Name;
	uint32 Offset;
	uint32 Size;
};

class Shader
{
public:
	enum class Type
	{
		VertexShader,
		PixelShader,
		ComputeShader,
		MAX
	};

	Shader(const char* pFilePath, Type shaderType, const char* pEntryPoint, const std::vector<std::string> defines = {});

	inline Type GetType() const { return m_Type; }
	inline void* GetByteCode() const { return m_pByteCode->GetBufferPointer(); }
	inline uint32 GetByteCodeSize() const { return (uint32)m_pByteCode->GetBufferSize(); }

	const ShaderParameter& GetShaderParameter(const std::string& name) const { return m_Parameters.at(name); }

	static void AddGlobalShaderDefine(const std::string& name, const std::string& value = "1");

private:
	static std::vector<std::pair<std::string, std::string>> m_GlobalShaderDefines;

	std::map<std::string, ShaderParameter> m_Parameters;

	ComPtr<ID3DBlob> m_pByteCode;
	Type m_Type;
};

