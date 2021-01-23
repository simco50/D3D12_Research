#pragma once
#include "dxc/dxcapi.h"
#include <set>

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
	Shader(const ShaderBlob& shaderBlob, ShaderType shaderType, const std::string& entryPoint, const std::vector<ShaderDefine>& defines)
		: ShaderBase(shaderBlob, defines), m_Type(shaderType), m_EntryPoint(entryPoint)
	{
	}
	inline ShaderType GetType() const { return m_Type; }
	inline const std::string& GetEntryPoint() const { return m_EntryPoint; }

private:
	ShaderType m_Type;
	std::string m_EntryPoint;
};

class ShaderLibrary : public ShaderBase
{
public:
	ShaderLibrary(const ShaderBlob& shaderBlob, const std::vector<ShaderDefine>& defines)
		: ShaderBase(shaderBlob, defines)
	{}
};

DECLARE_MULTICAST_DELEGATE(OnShaderRecompiled, Shader* /*pOldShader*/, Shader* /*pRecompiledShader*/);
DECLARE_MULTICAST_DELEGATE(OnLibraryRecompiled, ShaderLibrary* /*pOldShader*/, ShaderLibrary* /*pRecompiledShader*/);

class ShaderManager
{
public:
	ShaderManager(const std::string& shaderSourcePath, uint8 shaderModelMaj, uint8 shaderModelMin);
	~ShaderManager();

	void ConditionallyReloadShaders();

	Shader* GetShader(const std::string& shaderPath, ShaderType shaderType, const std::string& entryPoint, const std::vector<ShaderDefine>& defines = {});
	ShaderLibrary* GetLibrary(const std::string& shaderPath, const std::vector<ShaderDefine>& defines = {});

	OnShaderRecompiled& OnShaderRecompiledEvent() { return m_OnShaderRecompiledEvent; }
	OnLibraryRecompiled& OnLibraryRecompiledEvent() { return m_OnLibraryRecompiledEvent; }

	void DrawImGuiStats();

private:
	void RecompileFromFileChange(const std::string& filePath);
	static bool ProcessSource(const std::string& sourcePath, const std::string& filePath, std::stringstream& output, std::vector<std::string>& processedIncludes, std::vector<std::string>& dependencies);

	std::unique_ptr<FileWatcher> m_pFileWatcher;

	using ShaderPtr = std::unique_ptr<Shader>;
	using LibraryPtr = std::unique_ptr<ShaderLibrary>;

	std::vector<ShaderPtr> m_Shaders;
	std::vector<LibraryPtr> m_Libraries;

	std::unordered_map<std::string, std::set<std::string>> m_IncludeDependencyMap;

	struct ShadersInFileMap
	{
		using EntryPointAndDefinesHash = StringHash;
		std::unordered_map<EntryPointAndDefinesHash, Shader*> Shaders;
		std::unordered_map<EntryPointAndDefinesHash, ShaderLibrary*> Libraries;
	};
	std::unordered_map<std::string, ShadersInFileMap> m_FilepathToObjectMap;
	
	std::string m_ShaderSourcePath;
	uint8 m_ShaderModelMajor;
	uint8 m_ShaderModelMinor;

	OnShaderRecompiled m_OnShaderRecompiledEvent;
	OnLibraryRecompiled m_OnLibraryRecompiledEvent;
};
