#pragma once
#include "dxc/dxcapi.h"

using ShaderBlob = ComPtr<IDxcBlob>;

enum class ShaderType
{
	Vertex,
	Pixel,
	Geometry,
	Hull,
	Domain,
	Mesh,
	Amplification,
	Compute,
	MAX,
};

class ShaderBase
{
public:
	ShaderBase(const ShaderBlob& shaderBlob)
		: m_pByteCode(shaderBlob)
	{}

	void* GetByteCode() const;
	virtual ~ShaderBase();
	uint32 GetByteCodeSize() const;

protected:
	ShaderBlob m_pByteCode;
};

class Shader : public ShaderBase
{
public:
	Shader(const ShaderBlob& shaderBlob, ShaderType shaderType, const std::string& entryPoint)
		: ShaderBase(shaderBlob), m_Type(shaderType), m_EntryPoint(entryPoint)
	{
	}
	inline ShaderType GetType() const { return m_Type; }

private:
	ShaderType m_Type;
	std::string m_EntryPoint;
};

class ShaderLibrary : public ShaderBase
{
public:
	ShaderLibrary(const ShaderBlob& shaderBlob)
		: ShaderBase(shaderBlob)
	{}
};

class ShaderManager
{
public:
	ShaderManager(const std::string& shaderSourcePath, uint8 shaderModelMaj, uint8 shaderModelMin);

	Shader* GetShader(const std::string& shaderPath, ShaderType shaderType, const char* pEntryPoint, const std::vector<std::string>& defines = {});
	ShaderLibrary* GetLibrary(const std::string& shaderPath, const std::vector<std::string>& defines = {});
private:
	static bool ProcessSource(const std::string& sourcePath, const std::string& filePath, std::stringstream& output, std::vector<StringHash>& processedIncludes, std::vector<std::string>& dependencies);

	using ShaderPtr = std::unique_ptr<Shader>;
	using LibraryPtr = std::unique_ptr<ShaderLibrary>;

	std::vector<ShaderPtr> m_Shaders;
	std::vector<LibraryPtr> m_Libraries;

	std::string m_ShaderSourcePath;
	uint8 m_ShaderModelMajor;
	uint8 m_ShaderModelMinor;
};
