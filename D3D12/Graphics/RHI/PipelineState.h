#pragma once
#include "GraphicsResource.h"
#include "Shader.h"

class ShaderManager;
class RootSignature;

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
private:
#define SUBOBJECT_TRAIT(value, type) \
	template<> struct CD3DX12_PIPELINE_STATE_SUBOJECT_TYPE_TRAITS<value> \
	{ \
		using Type = type; \
	};

	template<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE T> struct CD3DX12_PIPELINE_STATE_SUBOJECT_TYPE_TRAITS;
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS, D3D12_PIPELINE_STATE_FLAGS)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK, UINT)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE, ID3D12RootSignature*)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT, D3D12_INPUT_LAYOUT_DESC)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE, D3D12_INDEX_BUFFER_STRIP_CUT_VALUE)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY, D3D12_PRIMITIVE_TOPOLOGY_TYPE)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS, D3D12_SHADER_BYTECODE)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS, D3D12_SHADER_BYTECODE)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT, D3D12_STREAM_OUTPUT_DESC)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS, D3D12_SHADER_BYTECODE)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS, D3D12_SHADER_BYTECODE)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, D3D12_SHADER_BYTECODE)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS, D3D12_SHADER_BYTECODE)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND, CD3DX12_BLEND_DESC)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL, CD3DX12_DEPTH_STENCIL_DESC)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1, CD3DX12_DEPTH_STENCIL_DESC1)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT, DXGI_FORMAT)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER, CD3DX12_RASTERIZER_DESC)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS, D3D12_RT_FORMAT_ARRAY)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC, DXGI_SAMPLE_DESC)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK, UINT)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO, D3D12_CACHED_PIPELINE_STATE)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING, CD3DX12_VIEW_INSTANCING_DESC)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS, D3D12_SHADER_BYTECODE)
	SUBOBJECT_TRAIT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS, D3D12_SHADER_BYTECODE)
#undef SUBOBJECT_TRAIT

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
	void SetStencilTest(bool stencilEnabled, D3D12_COMPARISON_FUNC mode, D3D12_STENCIL_OP pass, D3D12_STENCIL_OP fail, D3D12_STENCIL_OP zFail, unsigned int stencilRef, unsigned char compareMask, unsigned char writeMask);

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
	void SetGeometryShader(const char* pShaderPath, const char* entryPoint = "", const Span<ShaderDefine>& defines = {});
	void SetComputeShader(const char* pShaderPath, const char* entryPoint = "", const Span<ShaderDefine>& defines = {});
	void SetMeshShader(const char* pShaderPath, const char* entryPoint = "", const Span<ShaderDefine>& defines = {});
	void SetAmplificationShader(const char* pShaderPath, const char* entryPoint = "", const Span<ShaderDefine>& defines = {});

	D3D12_PIPELINE_STATE_STREAM_DESC GetDesc(GraphicsDevice* pDevice);

private:
	template<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE ObjectType>
	typename CD3DX12_PIPELINE_STATE_SUBOJECT_TYPE_TRAITS<ObjectType>::Type& GetSubobject()
	{
		using InnerType = typename CD3DX12_PIPELINE_STATE_SUBOJECT_TYPE_TRAITS<ObjectType>::Type;
		struct SubobjectType
		{
			D3D12_PIPELINE_STATE_SUBOBJECT_TYPE ObjType;
			InnerType ObjectData;
		};
		if (m_pSubobjectLocations[ObjectType] < 0)
		{
			SubobjectType* pType = (SubobjectType*)(m_pSubobjectData.data() + m_Size);
			pType->ObjType = ObjectType;
			m_pSubobjectLocations[ObjectType] = m_Size;
			const auto AlignUp = [](uint32 value, uint32 alignment) {return (value + (alignment - 1)) & ~(alignment - 1); };
			m_Size += AlignUp(sizeof(SubobjectType), sizeof(void*));
			m_Subobjects++;
		}
		int offset = m_pSubobjectLocations[ObjectType];
		SubobjectType* pObj = (SubobjectType*)&m_pSubobjectData[offset];
		return pObj->ObjectData;
	}

	std::string m_Name;
	std::vector<D3D12_INPUT_ELEMENT_DESC> m_IlDesc;
	PipelineStateType m_Type = PipelineStateType::MAX;
	std::array<Shader*, (int)ShaderType::MAX> m_Shaders{};
	std::array<ShaderDesc, (int)ShaderType::MAX> m_ShaderDescs{};

	std::array<int, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MAX_VALID> m_pSubobjectLocations{};
	std::vector<char> m_pSubobjectData{};
	uint32 m_Subobjects = 0;
	uint32 m_Size = 0;
};

class PipelineState : public GraphicsObject
{
public:
	PipelineState(GraphicsDevice* pParent);
	PipelineState(const PipelineState& rhs) = delete;
	PipelineState& operator=(const PipelineState& rhs) = delete;
	~PipelineState();
	ID3D12PipelineState* GetPipelineState() const { return m_pPipelineState.Get(); }
	void Create(const PipelineStateInitializer& initializer);
	void ConditionallyReload();
	PipelineStateType GetType() const { return m_Desc.m_Type; }

private:
	void OnShaderReloaded(Shader* pOldShader, Shader* pNewShader);
	RefCountPtr<ID3D12PipelineState> m_pPipelineState;

	PipelineStateInitializer m_Desc;
	DelegateHandle m_ReloadHandle;
	bool m_NeedsReload = false;
};
