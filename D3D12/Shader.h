#pragma once

class Shader
{
public:
	Shader() {}
	~Shader() {}

	enum class Type
	{
		VertexShader,
		PixelShader,
		MAX
	};

	bool Load(const char* pFilePath, Type shaderType, const char* pEntryPoint);

	inline Type GetType() const { return m_Type; }
	inline void* GetByteCode() const { return m_pByteCode->GetBufferPointer(); }
	inline uint32 GetByteCodeSize() const { return m_pByteCode->GetBufferSize(); }

private:
	ComPtr<ID3DBlob> m_pByteCode;
	Type m_Type;
};

