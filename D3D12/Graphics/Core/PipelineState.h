#pragma once

enum class BlendMode
{
	Replace = 0,
	Additive,
	Multiply,
	Alpha,
	AddAlpha,
	PreMultiplyAlpha,
	InverseDestinationAlpha,
	Subtract,
	SubtractAlpha,
	Undefined,
};

class StateObjectDesc
{
	class PODLinearAllocator
	{
	public:
		PODLinearAllocator(uint32 size)
			: m_Size(size), m_pData(new char[m_Size]), m_pCurrentData(m_pData)
		{
			memset(m_pData, 0, m_Size);
		}

		~PODLinearAllocator()
		{
			delete m_pData;
			m_pData = nullptr;
		}

		template<typename T>
		T* Allocate(int count = 1)
		{
			return (T*)Allocate(sizeof(T) * count);
		}

		char* Allocate(uint32 size)
		{
			check(size > 0);
			checkf(m_pCurrentData - m_pData - size <= m_Size, "Make allocator size larger");
			char* pData = m_pCurrentData;
			m_pCurrentData += size;
			return pData;
		}

		const char* Data() const
		{
			return m_pData;
		}

	private:
		uint32 m_Size;
		char* m_pData;
		char* m_pCurrentData;
	};

public:
	StateObjectDesc(D3D12_STATE_OBJECT_TYPE type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);

	uint32 AddLibrary(const void* pByteCode, uint32 byteCodeLength, const std::vector<std::string>& exports = {});
	uint32 AddHitGroup(const char* pHitGroupExport, const char* pClosestHitShaderImport = nullptr, const char* pAnyHitShaderImport = nullptr, const char* pIntersectionShaderImport = nullptr);
	uint32 AddStateAssociation(uint32 index, const std::vector<std::string>& exports);
	uint32 AddCollection(ID3D12StateObject* pStateObject, const std::vector<std::string>& exports = {});
	uint32 BindLocalRootSignature(const char* pExportName, ID3D12RootSignature* pRootSignature);
	uint32 SetRaytracingShaderConfig(uint32 maxPayloadSize, uint32 maxAttributeSize);
	uint32 SetRaytracingPipelineConfig(uint32 maxRecursionDepth, D3D12_RAYTRACING_PIPELINE_FLAGS flags);
	uint32 SetGlobalRootSignature(ID3D12RootSignature* pRootSignature);
	ComPtr<ID3D12StateObject> Finalize(const char* pName, ID3D12Device5* pDevice) const;

private:
	uint32 AddStateObject(void* pDesc, D3D12_STATE_SUBOBJECT_TYPE type);
	const D3D12_STATE_SUBOBJECT* GetSubobject(uint32 index) const
	{
		check(index < m_SubObjects);
		const D3D12_STATE_SUBOBJECT* pData = (D3D12_STATE_SUBOBJECT*)m_StateObjectAllocator.Data();
		return &pData[index];
	}

	PODLinearAllocator m_StateObjectAllocator;
	PODLinearAllocator m_ScratchAllocator;
	uint32 m_SubObjects = 0;
	D3D12_STATE_OBJECT_TYPE m_Type;
};

class PipelineState
{
public:
	PipelineState();
	PipelineState(const PipelineState& other);
	ID3D12PipelineState* GetPipelineState() const { return m_pPipelineState.Get(); }
	void Finalize(const char* pName, ID3D12Device* pDevice);

	void SetRenderTargetFormat(DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat, uint32 msaa);
	void SetRenderTargetFormats(DXGI_FORMAT* rtvFormats, uint32 count, DXGI_FORMAT dsvFormat, uint32 msaa);

	//BlendState
	void SetBlendMode(const BlendMode& blendMode, bool alphaToCoverage);

	//DepthStencilState
	void SetDepthEnabled(bool enabled);
	void SetDepthWrite(bool enabled);
	void SetDepthTest(const D3D12_COMPARISON_FUNC func);
	void SetStencilTest(bool stencilEnabled, D3D12_COMPARISON_FUNC mode, D3D12_STENCIL_OP pass, D3D12_STENCIL_OP fail, D3D12_STENCIL_OP zFail, unsigned int stencilRef, unsigned char compareMask, unsigned char writeMask);

	//RasterizerState
	void SetFillMode(D3D12_FILL_MODE fillMode);
	void SetCullMode(D3D12_CULL_MODE cullMode);
	void SetLineAntialias(bool lineAntiAlias);
	void SetDepthBias(int depthBias, float depthBiasClamp, float slopeScaledDepthBias);

	void SetInputLayout(D3D12_INPUT_ELEMENT_DESC* pElements, uint32 count);
	void SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE topology);

	void SetRootSignature(ID3D12RootSignature* pRootSignature);

	//Shaders
	void SetVertexShader(const void* pByteCode, uint32 byteCodeLength);
	void SetPixelShader(const void* pByteCode, uint32 byteCodeLength);
	void SetGeometryShader(const void* pByteCode, uint32 byteCodeLength);
	void SetComputeShader(const void* pByteCode, uint32 byteCodeLength);

protected:
	ComPtr<ID3D12PipelineState> m_pPipelineState;
	CD3DX12_PIPELINE_STATE_STREAM2 m_Desc = {};
};