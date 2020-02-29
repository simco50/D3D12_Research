#pragma once
#include "DynamicResourceAllocator.h"
#include "GraphicsResource.h"
class Graphics;
class GraphicsResource;
class Texture;
class OnlineDescriptorAllocator;
class RootSignature;
class GraphicsPipelineState;
class ComputePipelineState;
class DynamicResourceAllocator;
class Buffer;

enum class CommandListContext
{
	Graphics,
	Compute
};

enum class RenderTargetLoadAction : uint8
{
	DontCare,
	Load,
	Clear,
};
DEFINE_ENUM_FLAG_OPERATORS(RenderTargetLoadAction)

enum class RenderTargetStoreAction : uint8
{
	DontCare,
	Store,
	Resolve,
};
DEFINE_ENUM_FLAG_OPERATORS(RenderTargetStoreAction)

enum class RenderPassAccess : uint8
{
#define COMBINE_ACTIONS(load, store) (uint8)RenderTargetLoadAction::load << 2 | (uint8)RenderTargetStoreAction::store
	DontCare_DontCare = COMBINE_ACTIONS(DontCare, DontCare),
	DontCare_Store = COMBINE_ACTIONS(DontCare, Store),
	Clear_Store = COMBINE_ACTIONS(Clear, Store),
	Load_Store = COMBINE_ACTIONS(Load, Store),
	Clear_DontCare = COMBINE_ACTIONS(Clear, DontCare),
	Load_DontCare = COMBINE_ACTIONS(Load, DontCare),
	Clear_Resolve = COMBINE_ACTIONS(Clear, Resolve),
	Load_Resolve = COMBINE_ACTIONS(Load, Resolve),
#undef COMBINE_ACTIONS
};

struct RenderPassInfo
{
	struct RenderTargetInfo
	{
		RenderPassAccess Access = RenderPassAccess::DontCare_DontCare;
		Texture* Target = nullptr;
		Texture* ResolveTarget = nullptr;
		int MipLevel = 0;
		int ArrayIndex = 0;
	};

	struct DepthTargetInfo
	{
		RenderPassAccess Access = RenderPassAccess::DontCare_DontCare;
		RenderPassAccess StencilAccess = RenderPassAccess::DontCare_DontCare;
		Texture* Target = nullptr;
	};

	RenderPassInfo()
	{
	}

	RenderPassInfo(Texture* pDepthBuffer, RenderPassAccess access, bool uavWrites = false)
		: RenderTargetCount(0)
	{
		DepthStencilTarget.Access = access;
		DepthStencilTarget.Target = pDepthBuffer;
		WriteUAVs = uavWrites;
	}

	RenderPassInfo(Texture* pRenderTarget, RenderPassAccess renderTargetAccess, Texture* pDepthBuffer, RenderPassAccess depthAccess, bool uavWrites = false, RenderPassAccess stencilAccess = RenderPassAccess::DontCare_DontCare)
		: RenderTargetCount(1)
	{
		RenderTargets[0].Access = renderTargetAccess;
		RenderTargets[0].Target = pRenderTarget;
		DepthStencilTarget.Access = depthAccess;
		DepthStencilTarget.Target = pDepthBuffer;
		DepthStencilTarget.StencilAccess = stencilAccess;
		WriteUAVs = uavWrites;
	}

	static RenderTargetLoadAction GetBeginAccess(RenderPassAccess access)
	{
		return (RenderTargetLoadAction)((uint8)access >> 2);
	}

	static RenderTargetStoreAction GetEndAccess(RenderPassAccess access)
	{
		return (RenderTargetStoreAction)((uint8)access & 0b11);
	}

	bool WriteUAVs = false;
	uint32 RenderTargetCount = 0;
	std::array<RenderTargetInfo, 4> RenderTargets{};
	DepthTargetInfo DepthStencilTarget{};
};

class CommandContext : public GraphicsObject
{
public:
	CommandContext(Graphics* pGraphics, ID3D12GraphicsCommandList* pCommandList, ID3D12CommandAllocator* pAllocator, D3D12_COMMAND_LIST_TYPE type);
	~CommandContext();

	void Reset();
	uint64 Execute(bool wait);

	void InsertResourceBarrier(GraphicsResource* pBuffer, D3D12_RESOURCE_STATES state, bool executeImmediate = false);
	void InsertUavBarrier(GraphicsResource* pBuffer = nullptr, bool executeImmediate = false);
	void FlushResourceBarriers();

	void CopyResource(GraphicsResource* pSource, GraphicsResource* pTarget);
	void InitializeBuffer(Buffer* pResource, const void* pData, uint64 dataSize, uint64 offset = 0);
	void InitializeTexture(Texture* pResource, D3D12_SUBRESOURCE_DATA* pSubResourceDatas, int firstSubResource, int subResourceCount);

	ID3D12GraphicsCommandList* GetCommandList() const { return m_pCommandList; }

	D3D12_COMMAND_LIST_TYPE GetType() const { return m_Type; }

	//Commands
	void Dispatch(uint32 groupCountX, uint32 groupCountY = 1, uint32 groupCountZ = 1);
	void ExecuteIndirect(ID3D12CommandSignature* pCommandSignature, Buffer* pIndirectArguments);
	void Draw(int vertexStart, int vertexCount);
	void DrawIndexed(int indexCount, int indexStart, int minVertex = 0);
	void DrawIndexedInstanced(int indexCount, int indexStart, int instanceCount, int minVertex = 0, int instanceStart = 0);
	void ClearRenderTarget(D3D12_CPU_DESCRIPTOR_HANDLE rtv, const Color& color = Color(0.0f, 0.0f, 0.0f, 1.0f));
	void ClearDepth(D3D12_CPU_DESCRIPTOR_HANDLE dsv, D3D12_CLEAR_FLAGS clearFlags = D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, float depth = 1.0f, unsigned char stencil = 0);
	void ResolveResource(Texture* pSource, uint32 sourceSubResource, Texture* pTarget, uint32 targetSubResource, DXGI_FORMAT format);

	void BeginRenderPass(const RenderPassInfo& renderPassInfo);
	void EndRenderPass();

	void ClearUavUInt(GraphicsResource* pBuffer, UnorderedAccessView* pUav, uint32* values = nullptr);
	void ClearUavFloat(GraphicsResource* pBuffer, UnorderedAccessView* pUav, float* values = nullptr);

	//Bindings
	void SetComputePipelineState(ComputePipelineState* pPipelineState);
	void SetComputeRootSignature(RootSignature* pRootSignature);
	void SetComputeRootConstants(int rootIndex, uint32 count, const void* pConstants);
	void SetComputeDynamicConstantBufferView(int rootIndex, void* pData, uint32 dataSize);

	void SetDynamicDescriptor(int rootIndex, int offset, D3D12_CPU_DESCRIPTOR_HANDLE handle);
	void SetDynamicDescriptor(int rootIndex, int offset, UnorderedAccessView* pView);
	void SetDynamicDescriptor(int rootIndex, int offset, ShaderResourceView* pView);
	void SetDynamicDescriptors(int rootIndex, int offset, D3D12_CPU_DESCRIPTOR_HANDLE* handles, int count = 1);
	void SetDynamicSampler(int rootIndex, int offset, D3D12_CPU_DESCRIPTOR_HANDLE handle);
	void SetDynamicSamplers(int rootIndex, int offset, D3D12_CPU_DESCRIPTOR_HANDLE* handles, int count = 1);

	void SetGraphicsPipelineState(GraphicsPipelineState* pPipelineState);
	void SetGraphicsRootSignature(RootSignature* pRootSignature);

	void SetGraphicsRootConstants(int rootIndex, uint32 count, const void* pConstants);
	void SetDynamicConstantBufferView(int rootIndex, const void* pData, uint32 dataSize);
	void SetDynamicVertexBuffer(int slot, int elementCount, int elementSize, const void* pData);
	void SetDynamicIndexBuffer(int elementCount, const void* pData, bool smallIndices = false);
	void SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY type);
	void SetVertexBuffer(Buffer* pVertexBuffer);
	void SetVertexBuffers(Buffer** pVertexBuffers, int bufferCount);
	void SetIndexBuffer(Buffer* pIndexBuffer);
	void SetViewport(const FloatRect& rect, float minDepth = 0.0f, float maxDepth = 1.0f);
	void SetScissorRect(const FloatRect& rect);

	void SetDescriptorHeap(ID3D12DescriptorHeap* pHeap, D3D12_DESCRIPTOR_HEAP_TYPE type);

private:
	std::unique_ptr<OnlineDescriptorAllocator> m_pShaderResourceDescriptorAllocator;
	std::unique_ptr<OnlineDescriptorAllocator> m_pSamplerDescriptorAllocator;

	std::array<ID3D12DescriptorHeap*, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES> m_CurrentDescriptorHeaps = {};

	static const int MAX_QUEUED_BARRIERS = 12;

	std::array<D3D12_RESOURCE_BARRIER, MAX_QUEUED_BARRIERS> m_QueuedBarriers = {};
	int m_NumQueuedBarriers = 0;

	std::unique_ptr<DynamicResourceAllocator> m_DynamicAllocator;
	ID3D12GraphicsCommandList* m_pCommandList;
	ID3D12CommandAllocator* m_pAllocator;
	D3D12_COMMAND_LIST_TYPE m_Type;

	void BindDescriptorHeaps();

	RenderPassInfo m_CurrentRenderPassInfo;
	bool m_InRenderPass = false;
};