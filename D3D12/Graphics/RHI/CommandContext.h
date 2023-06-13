#pragma once
#include "RHI.h"
#include "GraphicsResource.h"
#include "GPUDescriptorHeap.h"
#include "DynamicResourceAllocator.h"

struct VertexBufferView;
struct IndexBufferView;

enum class CommandListContext
{
	Graphics,
	Compute,
	Invalid,
};

enum class RenderTargetLoadAction : uint8
{
	DontCare,
	Load,
	Clear,
	NoAccess
};
DEFINE_ENUM_FLAG_OPERATORS(RenderTargetLoadAction)

enum class RenderTargetStoreAction : uint8
{
	DontCare,
	Store,
	Resolve,
	NoAccess,
};
DEFINE_ENUM_FLAG_OPERATORS(RenderTargetStoreAction)

constexpr uint8 CombineRenderTargetAction(RenderTargetLoadAction loadAction, RenderTargetStoreAction storeAction)
{
	return (uint8)loadAction << 4 | (uint8)storeAction;
}

enum class RenderPassAccess : uint8
{
	DontCare_DontCare =	CombineRenderTargetAction(RenderTargetLoadAction::DontCare,	RenderTargetStoreAction::DontCare),
	DontCare_Store =	CombineRenderTargetAction(RenderTargetLoadAction::DontCare,	RenderTargetStoreAction::Store),
	Clear_Store =		CombineRenderTargetAction(RenderTargetLoadAction::Clear,	RenderTargetStoreAction::Store),
	Load_Store =		CombineRenderTargetAction(RenderTargetLoadAction::Load,		RenderTargetStoreAction::Store),
	Clear_DontCare =	CombineRenderTargetAction(RenderTargetLoadAction::Clear,	RenderTargetStoreAction::DontCare),
	Load_DontCare =		CombineRenderTargetAction(RenderTargetLoadAction::Load,		RenderTargetStoreAction::DontCare),
	Clear_Resolve =		CombineRenderTargetAction(RenderTargetLoadAction::Clear,	RenderTargetStoreAction::Resolve),
	Load_Resolve =		CombineRenderTargetAction(RenderTargetLoadAction::Load,		RenderTargetStoreAction::Resolve),
	DontCare_Resolve =	CombineRenderTargetAction(RenderTargetLoadAction::DontCare,	RenderTargetStoreAction::Resolve),
	NoAccess =			CombineRenderTargetAction(RenderTargetLoadAction::NoAccess,	RenderTargetStoreAction::NoAccess),
};

struct RenderPassInfo
{
	struct RenderTargetInfo
	{
		Texture* Target = nullptr;
		Texture* ResolveTarget = nullptr;
		RenderPassAccess Access = RenderPassAccess::DontCare_DontCare;
		int MipLevel = 0;
		int ArrayIndex = 0;
	};

	struct DepthTargetInfo
	{
		Texture* Target = nullptr;
		RenderPassAccess Access = RenderPassAccess::NoAccess;
		RenderPassAccess StencilAccess = RenderPassAccess::NoAccess;
		bool Write = true;
	};

	RenderPassInfo() = default;

	RenderPassInfo(Texture* pRenderTarget, RenderPassAccess renderTargetAccess, Texture* pDepthBuffer, RenderPassAccess depthAccess, bool depthWrite, bool uavWrites = false, RenderPassAccess stencilAccess = RenderPassAccess::NoAccess)
		: RenderTargetCount(1)
	{
		RenderTargets[0].Access = renderTargetAccess;
		RenderTargets[0].Target = pRenderTarget;
		DepthStencilTarget.Access = depthAccess;
		DepthStencilTarget.Target = pDepthBuffer;
		DepthStencilTarget.StencilAccess = stencilAccess;
		DepthStencilTarget.Write = depthWrite;
		WriteUAVs = uavWrites;
	}

	static RenderPassInfo DepthOnly(Texture* pDepthTarget, RenderPassAccess depthAccess, RenderPassAccess stencilAccess = RenderPassAccess::NoAccess, bool uavWrites = false)
	{
		RenderPassInfo result;
		result.DepthStencilTarget.Access = depthAccess;
		result.DepthStencilTarget.Target = pDepthTarget;
		result.DepthStencilTarget.StencilAccess = stencilAccess;
		result.DepthStencilTarget.Write = true;
		result.WriteUAVs = uavWrites;
		return result;
	}

	static RenderTargetLoadAction GetBeginAccess(RenderPassAccess access)
	{
		return (RenderTargetLoadAction)((uint8)access >> 4);
	}

	static RenderTargetStoreAction GetEndAccess(RenderPassAccess access)
	{
		return (RenderTargetStoreAction)((uint8)access & 0xF);
	}

	bool WriteUAVs = false;
	uint32 RenderTargetCount = 0;
	std::array<RenderTargetInfo, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT> RenderTargets{};
	DepthTargetInfo DepthStencilTarget{};
};

namespace ComputeUtils
{
	inline Vector3i GetNumThreadGroups(uint32 threadsX = 1, uint32 groupSizeX = 1, uint32 threadsY = 1, uint32 groupSizeY = 1, uint32 threadsZ = 1, uint32 groupSizeZ = 1)
	{
		Vector3i groups;
		groups.x = Math::DivideAndRoundUp(threadsX, groupSizeX);
		groups.y = Math::DivideAndRoundUp(threadsY, groupSizeY);
		groups.z = Math::DivideAndRoundUp(threadsZ, groupSizeZ);
		return groups;
	}

	inline Vector3i GetNumThreadGroups(const Vector3i& threads, const Vector3i& threadGroupSize)
	{
		Vector3i groups;
		groups.x = Math::DivideAndRoundUp(threads.x, threadGroupSize.x);
		groups.y = Math::DivideAndRoundUp(threads.y, threadGroupSize.y);
		groups.z = Math::DivideAndRoundUp(threads.z, threadGroupSize.z);
		return groups;
	}
}

class CommandContext : public GraphicsObject
{
public:
	CommandContext(GraphicsDevice* pParent, RefCountPtr<ID3D12CommandList> pCommandList, D3D12_COMMAND_LIST_TYPE type, GPUDescriptorHeap* pDescriptorHeap, DynamicAllocationManager* pDynamicMemoryManager);

	void Reset();
	SyncPoint Execute();
	static SyncPoint Execute(const Span<CommandContext* const>& contexts);
	void Free(const SyncPoint& syncPoint);

	void InsertResourceBarrier(GraphicsResource* pBuffer, D3D12_RESOURCE_STATES state, uint32 subResource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
	void InsertUAVBarrier(const GraphicsResource* pBuffer = nullptr);
	void FlushResourceBarriers();

	void CopyResource(const GraphicsResource* pSource, const GraphicsResource* pTarget);
	void CopyTexture(const Texture* pSource, const Buffer* pDestination, const D3D12_BOX& sourceRegion, uint32 sourceSubregion = 0, uint32 destinationOffset = 0);
	void CopyTexture(const Texture* pSource, const Texture* pDestination, const D3D12_BOX& sourceRegion, const D3D12_BOX& destinationRegion, uint32 sourceSubregion = 0, uint32 destinationSubregion = 0);
	void CopyBuffer(const Buffer* pSource, const Buffer* pDestination, uint64 size, uint64 sourceOffset, uint64 destinationOffset);
	void WriteBuffer(const Buffer* pResource, const void* pData, uint64 dataSize, uint64 offset = 0);
	void WriteTexture(Texture* pResource, const Span<D3D12_SUBRESOURCE_DATA>& subResourceDatas, uint32 firstSubResource);

	void Dispatch(uint32 groupCountX, uint32 groupCountY = 1, uint32 groupCountZ = 1);
	void Dispatch(const Vector3i& groupCounts);
	void DispatchMesh(uint32 groupCountX, uint32 groupCountY = 1, uint32 groupCountZ = 1);
	void DispatchMesh(const Vector3i& groupCounts);
	void ExecuteIndirect(const CommandSignature* pCommandSignature, uint32 maxCount, const Buffer* pIndirectArguments, const Buffer* pCountBuffer = nullptr, uint32 argumentsOffset = 0, uint32 countOffset = 0);
	void Draw(uint32 vertexStart, uint32 vertexCount, uint32 instances = 1, uint32 instanceStart = 0);
	void DrawIndexedInstanced(uint32 indexCount, uint32 indexStart, uint32 instanceCount, uint32 minVertex = 0, uint32 instanceStart = 0);
	void DispatchRays(ShaderBindingTable& table, uint32 width = 1, uint32 height = 1, uint32 depth = 1);

	void ClearColor(D3D12_CPU_DESCRIPTOR_HANDLE rtv, const Color& color = Color(0.0f, 0.0f, 0.0f, 1.0f));
	void ClearDepth(D3D12_CPU_DESCRIPTOR_HANDLE dsv, D3D12_CLEAR_FLAGS clearFlags = D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, float depth = 1.0f, unsigned char stencil = 0);
	void ResolveResource(Texture* pSource, uint32 sourceSubResource, Texture* pTarget, uint32 targetSubResource, ResourceFormat format);

	void BeginRenderPass(const RenderPassInfo& renderPassInfo);
	void EndRenderPass();

	void ClearUAVu(const UnorderedAccessView* pUAV, const Vector4u& values = Vector4u::Zero());
	void ClearUAVf(const UnorderedAccessView* pUAV, const Vector4& values = Vector4::Zero);

	void SetPipelineState(PipelineState* pPipelineState);
	void SetPipelineState(StateObject* pStateObject);

	void SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY type);
	void SetVertexBuffers(const Span<VertexBufferView>& buffers);
	void SetIndexBuffer(const IndexBufferView& indexBuffer);
	void SetViewport(const FloatRect& rect, float minDepth = 0.0f, float maxDepth = 1.0f);
	void SetScissorRect(const FloatRect& rect);
	void SetStencilRef(uint32 stencilRef);

	void SetShadingRate(D3D12_SHADING_RATE shadingRate = D3D12_SHADING_RATE_1X1);
	void SetShadingRateImage(Texture* pTexture);

	void SetGraphicsRootSignature(const RootSignature* pRootSignature);
	void SetComputeRootSignature(const RootSignature* pRootSignature);

	void BindDynamicVertexBuffer(uint32 slot, uint32 elementCount, uint32 elementSize, const void* pData);
	void BindDynamicIndexBuffer(uint32 elementCount, const void* pData, ResourceFormat format);
	void BindResources(uint32 rootIndex, const Span<const ResourceView*>& pViews, uint32 offset = 0);
	void BindRootSRV(uint32 rootIndex, D3D12_GPU_VIRTUAL_ADDRESS address);
	void BindRootUAV(uint32 rootIndex, D3D12_GPU_VIRTUAL_ADDRESS address);
	void BindRootCBV(uint32 rootIndex, const void* pData, uint32 dataSize);
	template<typename T>
	void BindRootCBV(uint32 rootIndex, const T& data)
	{
		static_assert(!std::is_pointer_v<T>, "Provided type is a pointer. This is probably unintentional.");
		BindRootCBV(rootIndex, &data, sizeof(T));
	}

	DynamicAllocation AllocateTransientMemory(uint64 size, uint32 alignment = 16u);

	ID3D12GraphicsCommandList6* GetCommandList() const { return m_pCommandList; }

	D3D12_COMMAND_LIST_TYPE GetType() const { return m_Type; }
	const PipelineState* GetCurrentPSO() const { return m_pCurrentPSO; }
	void ResolvePendingBarriers(CommandContext& resolveContext);

private:
	void PrepareDraw();
	void AddBarrier(const D3D12_RESOURCE_BARRIER& barrier);

	static bool IsTransitionAllowed(D3D12_COMMAND_LIST_TYPE commandlistType, D3D12_RESOURCE_STATES state);
	D3D12_RESOURCE_STATES GetLocalResourceState(const GraphicsResource* pResource, uint32 subResource) const
	{
		auto it = m_ResourceStates.find(pResource);
		check(it != m_ResourceStates.end());
		return it->second.Get(subResource);
	}
	struct PendingBarrier
	{
		GraphicsResource* pResource;
		ResourceState State;
		uint32 Subresource;
	};

	DynamicGPUDescriptorAllocator m_ShaderResourceDescriptorAllocator;
	DynamicResourceAllocator m_DynamicAllocator;

	RefCountPtr<ID3D12GraphicsCommandList6> m_pCommandList;
	RefCountPtr<ID3D12CommandAllocator> m_pAllocator;

	static constexpr uint32 MaxNumBatchedBarriers = 64;
	std::array<D3D12_RESOURCE_BARRIER, MaxNumBatchedBarriers> m_BatchedBarriers{};
	uint32 m_NumBatchedBarriers = 0;
	std::vector<PendingBarrier> m_PendingBarriers;
	std::unordered_map<const GraphicsResource*, ResourceState> m_ResourceStates;

	D3D12_COMMAND_LIST_TYPE m_Type;
	CommandListContext m_CurrentCommandContext = CommandListContext::Invalid;
	std::array<D3D12_RENDER_PASS_ENDING_ACCESS_RESOLVE_SUBRESOURCE_PARAMETERS, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT> m_ResolveSubResourceParameters{};
	RenderPassInfo m_CurrentRenderPassInfo;
	bool m_InRenderPass = false;

	const PipelineState* m_pCurrentPSO = nullptr;
	const StateObject* m_pCurrentSO = nullptr;
	const RootSignature* m_pCurrentRS = nullptr;
};

class CommandSignatureInitializer
{
public:
	void AddDispatch();
	void AddDispatchMesh();
	void AddDraw();
	void AddDrawIndexed();
	void AddConstants(uint32 numConstants, uint32 rootIndex, uint32 offset);
	void AddConstantBufferView(uint32 rootIndex);
	void AddShaderResourceView(uint32 rootIndex);
	void AddUnorderedAccessView(uint32 rootIndex);
	void AddVertexBuffer(uint32 slot);
	void AddIndexBuffer();

	D3D12_COMMAND_SIGNATURE_DESC GetDesc() const;

private:
	uint32 m_Stride = 0;
	std::vector<D3D12_INDIRECT_ARGUMENT_DESC> m_ArgumentDesc;
};

class CommandSignature : public GraphicsObject
{
public:
	CommandSignature(GraphicsDevice* pParent, ID3D12CommandSignature* pCmdSignature);
	ID3D12CommandSignature* GetCommandSignature() const { return m_pCommandSignature.Get(); }
private:
	RefCountPtr<ID3D12CommandSignature> m_pCommandSignature;
};
