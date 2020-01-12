#pragma once

struct IDxcBlob;

class Shader
{
public:
	enum class Type
	{
		VertexShader,
		PixelShader,
		GeometryShader,
		ComputeShader,
		MAX
	};

	Shader(const char* pFilePath, Type shaderType, const char* pEntryPoint, const std::vector<std::string> defines = {});
	~Shader();

	inline Type GetType() const { return m_Type; }
	void* GetByteCode() const;
	uint32 GetByteCodeSize() const;

	static void AddGlobalShaderDefine(const std::string& name, const std::string& value = "1");

private:
	bool ProcessSource(const std::string& filePath, std::stringstream& output, std::vector<StringHash>& processedIncludes, std::vector<std::string>& dependencies);

	static std::vector<std::pair<std::string, std::string>> m_GlobalShaderDefines;

	std::string m_Path;
	ComPtr<ID3DBlob> m_pByteCode;
	ComPtr<IDxcBlob> m_pByteCodeDxc;
	Type m_Type;
};