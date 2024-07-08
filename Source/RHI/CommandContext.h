#pragma once
#include "RHI.h"
#include "DeviceResource.h"
#include "GPUDescriptorHeap.h"
#include "ScratchAllocator.h"

struct VertexBufferView;
struct IndexBufferView;

enum class CommandListContext : uint8
{
	Graphics,
	Compute,
	Invalid,
};

enum class RenderPassColorFlags : uint8
{
	None,
	Clear = 1 << 0,
	Resolve = 1 << 1,
};
DEFINE_ENUM_FLAG_OPERATORS(RenderPassColorFlags)

enum class RenderPassDepthFlags : uint8
{
	None,
	ClearDepth = 1 << 0,
	ClearStencil = 1 << 1,
	ReadOnlyDepth = 1 << 2,
	ReadOnlyStencil = 1 << 3,

	ReadOnly = ReadOnlyDepth | ReadOnlyStencil,
	Clear = ClearDepth | ClearStencil,
};
DEFINE_ENUM_FLAG_OPERATORS(RenderPassDepthFlags)

struct RenderPassInfo
{
	struct RenderTargetInfo
	{
		Texture* Target = nullptr;
		Texture* ResolveTarget = nullptr;
		RenderPassColorFlags Flags = RenderPassColorFlags::None;
		int MipLevel = 0;
		int ArrayIndex = 0;
	};

	struct DepthTargetInfo
	{
		Texture* Target = nullptr;
		RenderPassDepthFlags Flags = RenderPassDepthFlags::None;
	};

	RenderPassInfo() = default;

	RenderPassInfo(Texture* pRenderTarget, RenderPassColorFlags colorFlags, Texture* pDepthBuffer, RenderPassDepthFlags depthFlags)
		: RenderTargetCount(1)
	{
		RenderTargets[0].Flags = colorFlags;
		RenderTargets[0].Target = pRenderTarget;
		DepthStencilTarget.Flags = RenderPassDepthFlags::None;
		DepthStencilTarget.Target = pDepthBuffer;
	}

	static RenderPassInfo DepthOnly(Texture* pDepthTarget, RenderPassDepthFlags depthFlags)
	{
		RenderPassInfo result;
		result.DepthStencilTarget.Flags = depthFlags;
		result.DepthStencilTarget.Target = pDepthTarget;
		return result;
	}

	uint32 RenderTargetCount = 0;
	StaticArray<RenderTargetInfo, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT> RenderTargets{};
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

class CommandContext : public DeviceObject
{
public:
	CommandContext(GraphicsDevice* pParent, Ref<ID3D12CommandList> pCommandList, D3D12_COMMAND_LIST_TYPE type, GPUDescriptorHeap* pDescriptorHeap, ScratchAllocationManager* pDynamicMemoryManager);

	void Reset();
	SyncPoint Execute();
	static SyncPoint Execute(Span<CommandContext* const> contexts);
	void Free(const SyncPoint& syncPoint);
	void ClearState();

	void InsertResourceBarrier(DeviceResource* pResource, D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState, uint32 subResource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
	void InsertAliasingBarrier(const DeviceResource* pResource);
	void InsertUAVBarrier(const DeviceResource* pResource = nullptr);
	void FlushResourceBarriers();

	void CopyResource(const DeviceResource* pSource, const DeviceResource* pTarget);
	void CopyTexture(const Texture* pSource, const Buffer* pDestination, const D3D12_BOX& sourceRegion, uint32 sourceSubregion = 0, uint32 destinationOffset = 0);
	void CopyTexture(const Texture* pSource, const Texture* pDestination, const D3D12_BOX& sourceRegion, const D3D12_BOX& destinationRegion, uint32 sourceSubregion = 0, uint32 destinationSubregion = 0);
	void CopyBuffer(const Buffer* pSource, const Buffer* pDestination, uint64 size, uint64 sourceOffset, uint64 destinationOffset);

	void Dispatch(uint32 groupCountX, uint32 groupCountY = 1, uint32 groupCountZ = 1);
	void Dispatch(const Vector3i& groupCounts);
	void DispatchMesh(uint32 groupCountX, uint32 groupCountY = 1, uint32 groupCountZ = 1);
	void DispatchMesh(const Vector3i& groupCounts);
	void ExecuteIndirect(const CommandSignature* pCommandSignature, uint32 maxCount, const Buffer* pIndirectArguments, const Buffer* pCountBuffer = nullptr, uint32 argumentsOffset = 0, uint32 countOffset = 0);
	void Draw(uint32 vertexStart, uint32 vertexCount, uint32 instances = 1, uint32 instanceStart = 0);
	void DrawIndexedInstanced(uint32 indexCount, uint32 indexStart, uint32 instanceCount, uint32 minVertex = 0, uint32 instanceStart = 0);
	void DispatchRays(ShaderBindingTable& table, uint32 width = 1, uint32 height = 1, uint32 depth = 1);
	void DispatchGraph(const D3D12_DISPATCH_GRAPH_DESC& graphDesc);

	void ResolveResource(Texture* pSource, uint32 sourceSubResource, Texture* pTarget, uint32 targetSubResource, ResourceFormat format);

	void BeginRenderPass(const RenderPassInfo& renderPassInfo);
	void EndRenderPass();

	void ClearUAVu(const UnorderedAccessView* pUAV, const Vector4u& values = Vector4u::Zero());
	void ClearUAVf(const UnorderedAccessView* pUAV, const Vector4& values = Vector4::Zero);

	void SetPipelineState(PipelineState* pPipelineState);
	void SetPipelineState(StateObject* pStateObject);
	void SetProgram(const D3D12_SET_PROGRAM_DESC& programDesc);

	void SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY type);
	void SetVertexBuffers(Span<VertexBufferView> buffers);
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
	void BindResources(uint32 rootIndex, Span<const ResourceView*> pViews, uint32 offset = 0);
	void BindRootSRV(uint32 rootIndex, D3D12_GPU_VIRTUAL_ADDRESS address);
	void BindRootUAV(uint32 rootIndex, D3D12_GPU_VIRTUAL_ADDRESS address);
	void BindRootCBV(uint32 rootIndex, const void* pData, uint32 dataSize);
	template<typename T>
	void BindRootCBV(uint32 rootIndex, const T& data)
	{
		static_assert(!std::is_pointer_v<T>, "Provided type is a pointer. This is probably unintentional.");
		BindRootCBV(rootIndex, &data, sizeof(T));
	}
	void BindRootCBV(uint32 rootIndex, const ScratchAllocation& allocation);

	ScratchAllocation AllocateScratch(uint64 size, uint32 alignment = 16u);

	ID3D12GraphicsCommandList10* GetCommandList() const { return m_pCommandList; }

	D3D12_COMMAND_LIST_TYPE GetType() const { return m_Type; }
	const PipelineState* GetCurrentPSO() const { return m_pCurrentPSO; }
	void ResolvePendingBarriers(CommandContext& resolveContext);
	void PrepareDraw();

private:
	void AddBarrier(const D3D12_RESOURCE_BARRIER& barrier);

	D3D12_RESOURCE_STATES GetLocalResourceState(const DeviceResource* pResource, uint32 subResource) const
	{
		auto it = m_ResourceStates.find(pResource);
		check(it != m_ResourceStates.end());
		return it->second.Get(subResource);
	}
	struct PendingBarrier
	{
		DeviceResource* pResource;
		D3D12_RESOURCE_STATES State;
		uint32 Subresource;
	};

	DynamicGPUDescriptorAllocator m_ShaderResourceDescriptorAllocator;
	ScratchAllocator m_ScratchAllocator;

	Ref<ID3D12GraphicsCommandList10> m_pCommandList;
	Ref<ID3D12CommandAllocator> m_pAllocator;

	Ref<ID3D12DescriptorHeap> m_pRTVHeap;
	uint32 m_RTVSize = 0;
	Ref<ID3D12DescriptorHeap> m_pDSVHeap;

	static constexpr uint32 MaxNumBatchedBarriers = 64;
	StaticArray<D3D12_RESOURCE_BARRIER, MaxNumBatchedBarriers> m_BatchedBarriers{};
	uint32 m_NumBatchedBarriers = 0;
	Array<PendingBarrier> m_PendingBarriers;
	HashMap<const DeviceResource*, ResourceState> m_ResourceStates;

	D3D12_COMMAND_LIST_TYPE m_Type;
	CommandListContext m_CurrentCommandContext = CommandListContext::Invalid;
	StaticArray<D3D12_RENDER_PASS_ENDING_ACCESS_RESOLVE_SUBRESOURCE_PARAMETERS, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT> m_ResolveSubResourceParameters{};
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
	Array<D3D12_INDIRECT_ARGUMENT_DESC> m_ArgumentDesc;
};

class CommandSignature : public DeviceObject
{
public:
	CommandSignature(GraphicsDevice* pParent, ID3D12CommandSignature* pCmdSignature);
	ID3D12CommandSignature* GetCommandSignature() const { return m_pCommandSignature.Get(); }
private:
	Ref<ID3D12CommandSignature> m_pCommandSignature;
};
