#pragma once

class FileWatcher;

using ShaderBlob = RefCountPtr<ID3DBlob>;

enum class ShaderType
{
	Vertex,
	Pixel,
	Geometry,
	Mesh,
	Amplification,
	Compute,
	MAX,
};

struct ShaderDefine
{
	ShaderDefine() = default;
	ShaderDefine(const char* pDefine, const char* pValue) : Value(Sprintf("%s=%s", pDefine, pValue)) {}
	ShaderDefine(const char* pDefine, const uint32 value) : Value(Sprintf("%s=%d", pDefine, value)) {}
	ShaderDefine(const std::string& define) : Value(define) { }
	ShaderDefine(const char* pDefine) : Value(pDefine) { }
	std::string Value;
};

class ShaderDefineHelper
{
public:
	void Set(const char* pName, const char* pValue)
	{
		Get(pName).Value = pValue;
	}
	void Set(const char* pName, uint32 value)
	{
		Get(pName).Value = Sprintf("%d", value);
	}
	void Set(const char* pName, int32 value)
	{
		Get(pName).Value = Sprintf("%d", value);
	}
	void Set(const char* pName, bool value)
	{
		Get(pName).Value = value ? "1" : "0";
	}

	std::vector<ShaderDefine> operator*() const
	{
		std::vector<ShaderDefine> defines;
		defines.reserve(Defines.size());
		for (const DefineData& v : Defines)
		{
			ShaderDefine& define = defines.emplace_back();
			define.Value = Sprintf("%s=%s", v.Name, v.Value.c_str());
		}
		return defines;
	}

private:
	struct DefineData
	{
		StringHash Hash;
		const char* Name;
		std::string Value;
	};

	DefineData& Get(const char* pName)
	{
		StringHash hash(pName);
		for (DefineData& v : Defines)
		{
			if (v.Hash == hash)
			{
				return v;
			}
		}
		return Defines.emplace_back(DefineData{ hash, pName, "" });
	}

	std::vector<DefineData> Defines;
};

struct ShaderLibrary
{
	uint64 Hash[2];
	ShaderBlob pByteCode;
	std::vector<ShaderDefine> Defines;
};

struct Shader : public ShaderLibrary
{
	ShaderType Type;
	std::string EntryPoint;
};

class ShaderManager
{
public:
	ShaderManager(uint8 shaderModelMaj, uint8 shaderModelMin);
	~ShaderManager();

	void ConditionallyReloadShaders();
	void AddIncludeDir(const std::string& includeDir);

	Shader* GetShader(const char* pShaderPath, ShaderType shaderType, const char* pEntryPoint, const Span<ShaderDefine>& defines = {}, bool force = false);
	ShaderLibrary* GetLibrary(const char* pShaderPath, const Span<ShaderDefine>& defines = {}, bool force = false);

	DECLARE_MULTICAST_DELEGATE(OnShaderRecompiled, Shader* /*pOldShader*/, Shader* /*pRecompiledShader*/);
	OnShaderRecompiled& OnShaderRecompiledEvent() { return m_OnShaderRecompiledEvent; }
	DECLARE_MULTICAST_DELEGATE(OnLibraryRecompiled, ShaderLibrary* /*pOldShader*/, ShaderLibrary* /*pRecompiledShader*/);
	OnLibraryRecompiled& OnLibraryRecompiledEvent() { return m_OnLibraryRecompiledEvent; }

private:
	using ShaderStringHash = TStringHash<false>;

	ShaderStringHash GetEntryPointHash(const char* pEntryPoint, const Span<ShaderDefine>& defines);

	void RecompileFromFileChange(const std::string& filePath);

	std::vector<std::string> m_IncludeDirs;

	std::unique_ptr<FileWatcher> m_pFileWatcher;

	std::vector<Shader*> m_Shaders;
	std::vector<ShaderLibrary*> m_Libraries;

	std::unordered_map<ShaderStringHash, std::unordered_set<std::string>> m_IncludeDependencyMap;

	struct ShadersInFileMap
	{
		std::unordered_map<ShaderStringHash, Shader*> Shaders;
		std::unordered_map<ShaderStringHash, ShaderLibrary*> Libraries;
	};
	std::unordered_map<ShaderStringHash, ShadersInFileMap> m_FilepathToObjectMap;

	uint8 m_ShaderModelMajor;
	uint8 m_ShaderModelMinor;

	std::mutex m_CompileMutex;
	OnShaderRecompiled m_OnShaderRecompiledEvent;
	OnLibraryRecompiled m_OnLibraryRecompiledEvent;
};
