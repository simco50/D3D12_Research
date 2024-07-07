#pragma once

class FileWatcher;

using ShaderBlob = Ref<ID3DBlob>;

enum class ShaderType
{
	Vertex,
	Pixel,
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
	ShaderDefine(const String& define) : Value(define) { }
	ShaderDefine(const char* pDefine) : Value(pDefine) { }
	String Value;
};

class ShaderDefineHelper
{
public:
	ShaderDefineHelper() = default;
	ShaderDefineHelper(const ShaderDefineHelper& parent)
		: pParent(&parent)
	{}

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

	Array<ShaderDefine> operator*() const
	{
		Array<ShaderDefine> defines;
		defines.reserve(Defines.size());
		Resolve(defines);
		return defines;
	}

private:
	void Resolve(Array<ShaderDefine>& outDefines) const
	{
		if (pParent)
			pParent->Resolve(outDefines);
		for (const DefineData& v : Defines)
		{
			ShaderDefine& define = outDefines.emplace_back();
			define.Value = Sprintf("%s=%s", v.Name, v.Value.c_str());
		}
	}

	struct DefineData
	{
		StringHash Hash;
		const char* Name;
		String Value;
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

	const ShaderDefineHelper* pParent = nullptr;
	Array<DefineData> Defines;
};

struct Shader
{
	uint64 Hash[2];
	ShaderBlob pByteCode;
	Array<ShaderDefine> Defines;
	ShaderType Type;
	String EntryPoint;
	bool IsDirty = false;
};

struct ShaderResult
{
	Shader* pShader;
	String Error;

	operator Shader* () const { return pShader; }
};

class ShaderManager
{
public:
	ShaderManager(uint8 shaderModelMaj, uint8 shaderModelMin);
	~ShaderManager();

	void ConditionallyReloadShaders();
	void AddIncludeDir(const String& includeDir);

	ShaderResult GetShader(const char* pShaderPath, ShaderType shaderType, const char* pEntryPoint, Span<ShaderDefine> defines = {});

	DECLARE_MULTICAST_DELEGATE(OnShaderEdited, Shader* /*pShader*/);
	OnShaderEdited& OnShaderEditedEvent() { return m_OnShaderEditedEvent; }

private:
	using ShaderStringHash = TStringHash<false>;

	ShaderStringHash GetEntryPointHash(const char* pEntryPoint, Span<ShaderDefine> defines);

	void RecompileFromFileChange(const String& filePath);

	Array<String> m_IncludeDirs;

	std::unique_ptr<FileWatcher> m_pFileWatcher;

	Array<Shader*> m_Shaders;

	HashMap<ShaderStringHash, HashSet<String>> m_IncludeDependencyMap;

	struct ShadersInFileMap
	{
		HashMap<ShaderStringHash, Shader*> Shaders;
	};
	HashMap<ShaderStringHash, ShadersInFileMap> m_FilepathToObjectMap;

	uint8 m_ShaderModelMajor;
	uint8 m_ShaderModelMinor;

	std::mutex m_CompileMutex;
	OnShaderEdited m_OnShaderEditedEvent;
};
