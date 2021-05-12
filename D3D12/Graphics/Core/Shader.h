#pragma once
#include "dxc/dxcapi.h"

#ifndef SHADER_HASH_DEBUG
#define SHADER_HASH_DEBUG 0
#endif

class FileWatcher;

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

struct ShaderDefine
{
	ShaderDefine() = default;
	ShaderDefine(const char* pDefine) : Value(pDefine) { }
	std::string Value;
};

class ShaderBase
{
public:
	ShaderBase(const ShaderBlob& shaderBlob, const std::vector<ShaderDefine>& defines)
		: m_pByteCode(shaderBlob), m_Defines(defines)
	{}

	void* GetByteCode() const;
	virtual ~ShaderBase();
	uint32 GetByteCodeSize() const;
	const std::vector<ShaderDefine>& GetDefines() const { return m_Defines; }

protected:
	ShaderBlob m_pByteCode;
	std::vector<ShaderDefine> m_Defines;
};

class Shader : public ShaderBase
{
public:
	Shader(const ShaderBlob& shaderBlob, ShaderType shaderType, const char* pEntryPoint, const std::vector<ShaderDefine>& defines)
		: ShaderBase(shaderBlob, defines), m_Type(shaderType), m_pEntryPoint(pEntryPoint)
	{
	}
	inline ShaderType GetType() const { return m_Type; }
	inline const char* GetEntryPoint() const { return m_pEntryPoint; }

private:
	ShaderType m_Type;
	const char* m_pEntryPoint;
};

class ShaderLibrary : public ShaderBase
{
public:
	ShaderLibrary(const ShaderBlob& shaderBlob, const std::vector<ShaderDefine>& defines)
		: ShaderBase(shaderBlob, defines)
	{}
};


class ShaderManager
{
public:
	ShaderManager(const char* pShaderSourcePath, uint8 shaderModelMaj, uint8 shaderModelMin);
	~ShaderManager();

	void ConditionallyReloadShaders();

	Shader* GetShader(const char* pShaderPath, ShaderType shaderType, const char* pEntryPoint, const std::vector<ShaderDefine>& defines = {});
	ShaderLibrary* GetLibrary(const char* pShaderPath, const std::vector<ShaderDefine>& defines = {});

	DECLARE_MULTICAST_DELEGATE(OnShaderRecompiled, Shader* /*pOldShader*/, Shader* /*pRecompiledShader*/);
	OnShaderRecompiled& OnShaderRecompiledEvent() { return m_OnShaderRecompiledEvent; }
	DECLARE_MULTICAST_DELEGATE(OnLibraryRecompiled, ShaderLibrary* /*pOldShader*/, ShaderLibrary* /*pRecompiledShader*/);
	OnLibraryRecompiled& OnLibraryRecompiledEvent() { return m_OnLibraryRecompiledEvent; }

private:

#if SHADER_HASH_DEBUG
	using ShaderStringHash = std::string;
#else
	using ShaderStringHash = StringHash;
#endif

	ShaderStringHash GetEntryPointHash(const char* pEntryPoint, const std::vector<ShaderDefine>& defines);
	Shader* LoadShader(const char* pShaderPath, ShaderType shaderType, const char* pEntryPoint, const std::vector<ShaderDefine>& defines = {});
	ShaderLibrary* LoadShaderLibrary(const char* pShaderPath, const std::vector<ShaderDefine>& defines = {});

	void RecompileFromFileChange(const std::string& filePath);
	static bool ProcessSource(const std::string& sourcePath, const std::string& filePath, std::stringstream& output, std::vector<ShaderStringHash>& processedIncludes);

	std::unique_ptr<FileWatcher> m_pFileWatcher;

	using ShaderPtr = std::unique_ptr<Shader>;
	using LibraryPtr = std::unique_ptr<ShaderLibrary>;

	std::list<ShaderPtr> m_Shaders;
	std::list<LibraryPtr> m_Libraries;

	std::unordered_map<ShaderStringHash, std::unordered_set<std::string>> m_IncludeDependencyMap;

	struct ShadersInFileMap
	{
		std::unordered_map<StringHash, Shader*> Shaders;
		std::unordered_map<StringHash, ShaderLibrary*> Libraries;
	};
	std::unordered_map<ShaderStringHash, ShadersInFileMap> m_FilepathToObjectMap;
	
	const char* m_pShaderSourcePath;
	uint8 m_ShaderModelMajor;
	uint8 m_ShaderModelMinor;

	OnShaderRecompiled m_OnShaderRecompiledEvent;
	OnLibraryRecompiled m_OnLibraryRecompiledEvent;
};
