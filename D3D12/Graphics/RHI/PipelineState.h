#pragma once
#include "GraphicsResource.h"
#include "Shader.h"
#include "Core/TaskQueue.h"

class ShaderManager;

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

enum class PipelineStateType
{
	Graphics,
	Compute,
	Mesh,
	MAX
};

struct VertexElementDesc
{
	const char* pSemantic;
	ResourceFormat Format;
	uint32 ByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	uint32 InputSlot = 0;
	uint32 InstanceStepRate = 0;
};

class PipelineStateInitializer
{
	friend class PipelineState;
public:
	PipelineStateInitializer();

	void SetName(const char* pName);
	void SetDepthOnlyTarget(ResourceFormat dsvFormat, uint32 msaa);
	void SetRenderTargetFormats(const Span<ResourceFormat>& rtvFormats, ResourceFormat dsvFormat, uint32 msaa);

	//BlendState
	void SetBlendMode(const BlendMode& blendMode, bool alphaToCoverage);

	//DepthStencilState
	void SetDepthEnabled(bool enabled);
	void SetDepthWrite(bool enabled);
	void SetDepthTest(D3D12_COMPARISON_FUNC func);
	void SetStencilTest(bool stencilEnabled, D3D12_COMPARISON_FUNC mode, D3D12_STENCIL_OP pass, D3D12_STENCIL_OP fail, D3D12_STENCIL_OP zFail, unsigned char compareMask, unsigned char writeMask);

	//RasterizerState
	void SetFillMode(D3D12_FILL_MODE fillMode);
	void SetCullMode(D3D12_CULL_MODE cullMode);
	void SetLineAntialias(bool lineAntiAlias);
	void SetDepthBias(int depthBias, float depthBiasClamp, float slopeScaledDepthBias);

	void SetInputLayout(const Span<VertexElementDesc>& layout);
	void SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE topology);

	void SetRootSignature(RootSignature* pRootSignature);

	struct ShaderDesc
	{
		std::string Path;
		std::string EntryPoint;
		std::vector<ShaderDefine> Defines;
	};

	//Shaders
	void SetVertexShader(const char* pShaderPath, const char* entryPoint = "", const Span<ShaderDefine>& defines = {});
	void SetPixelShader(const char* pShaderPath, const char* entryPoint = "", const Span<ShaderDefine>& defines = {});
	void SetComputeShader(const char* pShaderPath, const char* entryPoint = "", const Span<ShaderDefine>& defines = {});
	void SetMeshShader(const char* pShaderPath, const char* entryPoint = "", const Span<ShaderDefine>& defines = {});
	void SetAmplificationShader(const char* pShaderPath, const char* entryPoint = "", const Span<ShaderDefine>& defines = {});

	bool GetDesc(GraphicsDevice* pDevice, D3D12_PIPELINE_STATE_STREAM_DESC& outDesc);

private:
#pragma warning(push)
	#pragma warning(disable : 4324)
	template<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE SubObjectType, typename T>
	struct alignas(void*) StreamSubObject
	{
		StreamSubObject() = default;

		StreamSubObject(const T& rhs)
			: Type(SubObjectType), InnerObject(rhs)
		{}
		operator T&() { return InnerObject; }

	private:
		D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type = SubObjectType;
		T InnerObject{};
	};
	#pragma warning(pop)

	struct ObjectStream
	{
		StreamSubObject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS, D3D12_SHADER_BYTECODE> VS;
		StreamSubObject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, D3D12_SHADER_BYTECODE> PS;
		StreamSubObject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS, D3D12_SHADER_BYTECODE> CS;
		StreamSubObject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS, D3D12_SHADER_BYTECODE> AS;
		StreamSubObject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS, D3D12_SHADER_BYTECODE> MS;
		StreamSubObject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS, D3D12_RT_FORMAT_ARRAY> RTFormats;
		StreamSubObject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT, DXGI_FORMAT> DSVFormat;
		StreamSubObject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1, CD3DX12_DEPTH_STENCIL_DESC1> DepthStencil;
		StreamSubObject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER, CD3DX12_RASTERIZER_DESC> Rasterizer;
		StreamSubObject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND, CD3DX12_BLEND_DESC> Blend;
		StreamSubObject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY, D3D12_PRIMITIVE_TOPOLOGY_TYPE> PrimitiveTopology;
		StreamSubObject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT, D3D12_INPUT_LAYOUT_DESC> InputLayout;
		StreamSubObject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE, ID3D12RootSignature*> pRootSignature;
		StreamSubObject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK, UINT> SampleMask;
		StreamSubObject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC, DXGI_SAMPLE_DESC> SampleDesc;
		StreamSubObject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE, D3D12_INDEX_BUFFER_STRIP_CUT_VALUE> StripCutValue;
		StreamSubObject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT, D3D12_STREAM_OUTPUT_DESC> StreamOutput;
		StreamSubObject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS, D3D12_PIPELINE_STATE_FLAGS> Flags;
		StreamSubObject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK, UINT> NodeMask;
	} m_Stream;

	std::string m_Name;
	std::vector<D3D12_INPUT_ELEMENT_DESC> m_IlDesc;
	PipelineStateType m_Type = PipelineStateType::MAX;
	std::array<Shader*, (int)ShaderType::MAX> m_Shaders{};
	std::array<ShaderDesc, (int)ShaderType::MAX> m_ShaderDescs{};
};

class PipelineState : public GraphicsObject
{
public:
	PipelineState(GraphicsDevice* pParent);
	PipelineState(const PipelineState& rhs) = delete;
	PipelineState& operator=(const PipelineState& rhs) = delete;
	~PipelineState();
	ID3D12PipelineState* GetPipelineState() const { return m_pPipelineState; }
	void Create(const PipelineStateInitializer& initializer);
	void ConditionallyReload();
	PipelineStateType GetType() const { return m_Desc.m_Type; }

private:
	void OnShaderReloaded(Shader* pShader);
	RefCountPtr<ID3D12PipelineState> m_pPipelineState;

	PipelineStateInitializer m_Desc;
	DelegateHandle m_ReloadHandle;
	bool m_NeedsReload = false;
};
