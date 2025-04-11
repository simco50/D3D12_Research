#pragma once
#include "RHI.h"
#include "DeviceResource.h"
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
	None		= 0,
	Clear		= 1 << 0,
	Resolve		= 1 << 1,
};
DEFINE_ENUM_FLAG_OPERATORS(RenderPassColorFlags)

enum class RenderPassDepthFlags : uint8
{
	None			= 0,
	ClearDepth		= 1 << 0,
	ClearStencil	= 1 << 1,
	ReadOnlyDepth	= 1 << 2,
	ReadOnlyStencil = 1 << 3,

	ReadOnly		= ReadOnlyDepth | ReadOnlyStencil,
	Clear			= ClearDepth | ClearStencil,
};
DEFINE_ENUM_FLAG_OPERATORS(RenderPassDepthFlags)

struct RenderPassInfo
{
	struct RenderTargetInfo
	{
		Texture* pTarget = nullptr;
		Texture* pResolveTarget = nullptr;
		RenderPassColorFlags Flags = RenderPassColorFlags::None;
		uint8 MipLevel = 0;
		uint8 ArrayIndex = 0;
	};

	struct DepthTargetInfo
	{
		Texture* pTarget = nullptr;
		RenderPassDepthFlags Flags = RenderPassDepthFlags::None;
		uint8 MipLevel = 0;
		uint8 ArrayIndex = 0;
	};

	RenderPassInfo() = default;

	RenderPassInfo(Texture* pRenderTarget, RenderPassColorFlags colorFlags, Texture* pDepthBuffer, RenderPassDepthFlags depthFlags)
		: RenderTargetCount(1)
	{
		RenderTargets[0].Flags = colorFlags;
		RenderTargets[0].pTarget = pRenderTarget;
		DepthStencilTarget.Flags = depthFlags;
		DepthStencilTarget.pTarget = pDepthBuffer;
	}

	static RenderPassInfo DepthOnly(Texture* pDepthTarget, RenderPassDepthFlags depthFlags)
	{
		RenderPassInfo result;
		result.DepthStencilTarget.Flags = depthFlags;
		result.DepthStencilTarget.pTarget = pDepthTarget;
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
	CommandContext(GraphicsDevice* pParent, Ref<ID3D12CommandList> pCommandList, D3D12_COMMAND_LIST_TYPE type, ScratchAllocationManager* pScratchAllocationManager);

	void Reset();
	void Free(const SyncPoint& syncPoint);
	void ClearState();

	void InsertResourceBarrier(DeviceResource* pResource, D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState, uint32 subResource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
	void InsertAliasingBarrier(const DeviceResource* pResource);
	void InsertUAVBarrier(const DeviceResource* pResource = nullptr);
	void FlushResourceBarriers();

	void CopyResource(const DeviceResource* pSource, const DeviceResource* pTarget);
	void CopyTexture(const Texture* pSource, const Buffer* pDestination, const Vector3u& sourceOrigin, const Vector3u sourceSize, uint32 sourceMip = 0, uint32 sourceArrayIndex = 0, uint32 destinationOffset = 0);
	void CopyTexture(const Texture* pSource, const Texture* pDestination, const Vector3u& sourceOrigin, const Vector3u sourceSize, const Vector3u& destinationOrigin, uint32 sourceMip = 0, uint32 sourceArrayIndex = 0, uint32 destinationMip = 0, uint32 destinationArrayIndex = 0);
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

	void BeginRenderPass(const RenderPassInfo& renderPassInfo);
	void EndRenderPass();

	void ClearBufferFloat(const Buffer* pBuffer, float value = 0);
	void ClearBufferUInt(const Buffer* pBuffer, uint32 value = 0);
	void ClearTextureFloat(const Texture* pTexture, const Vector4& values = Vector4::Zero);
	void ClearTextureUInt(const Texture* pTexture, const Vector4u& values = Vector4u::Zero());
	void ClearRenderTarget(const Texture* pTexture, const Vector4& values = Vector4::Zero, uint32 mipLevel = 0, uint32 arrayIndex = 0);
	void ClearDepthStencil(const Texture* pTexture, RenderPassDepthFlags flags, float depth = 1.0f, uint8 stencil = 0, uint32 mipLevel = 0, uint32 arrayIndex = 0);

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

	void BindDynamicVertexBuffer(uint32 rootIndex, uint32 elementCount, uint32 elementSize, const void* pData);
	void BindDynamicIndexBuffer(uint32 elementCount, const void* pData, ResourceFormat format);

	void BindRootCBV(uint32 rootIndex, D3D12_GPU_VIRTUAL_ADDRESS address);
	void BindRootSRV(uint32 rootIndex, D3D12_GPU_VIRTUAL_ADDRESS address);
	void BindRootUAV(uint32 rootIndex, D3D12_GPU_VIRTUAL_ADDRESS address);

	void BindRootCBV(uint32 rootIndex, const void* pData, uint32 dataSize);
	void BindRootSRV(uint32 rootIndex, const void* pData, uint32 dataSize);

	template<typename T>
	void BindRootSRV(uint32 rootIndex, const T& data)
	{
		static_assert(!std::is_pointer_v<T>, "Provided type is a pointer. This is probably unintentional.");
		BindRootSRV(rootIndex, &data, sizeof(T));
	}

	template<typename T>
	void BindRootCBV(uint32 rootIndex, const T& data)
	{
		static_assert(!std::is_pointer_v<T>, "Provided type is a pointer. This is probably unintentional.");
		BindRootCBV(rootIndex, &data, sizeof(T));
	}

	ScratchAllocation AllocateScratch(uint64 size, uint32 alignment = 16u);

	ID3D12GraphicsCommandListX* GetCommandList() const { return m_pCommandList; }

	D3D12_COMMAND_LIST_TYPE GetType() const { return m_Type; }
	void ResolvePendingBarriers(CommandContext& resolveContext);

private:
	D3D12_CPU_DESCRIPTOR_HANDLE GetRTV(uint32 slot, const Texture* pTexture, uint32 mipLevel = 0, uint32 arrayIndex = 0);
	D3D12_CPU_DESCRIPTOR_HANDLE GetDSV(const Texture* pTexture, RenderPassDepthFlags flags, uint32 mipLevel = 0, uint32 arrayIndex = 0);

	void PrepareDraw();
	void ResolveResource(Texture* pSource, uint32 sourceSubResource, Texture* pTarget, uint32 targetSubResource, ResourceFormat format);
	void AddBarrier(const D3D12_RESOURCE_BARRIER& inBarrier);

	D3D12_RESOURCE_STATES GetLocalResourceState(const DeviceResource* pResource, uint32 subResource) const
	{
		auto it = m_ResourceStates.find(pResource);
		gAssert(it != m_ResourceStates.end());
		return it->second.Get(subResource);
	}
	struct PendingBarrier
	{
		DeviceResource*		  pResource;
		D3D12_RESOURCE_STATES State;
		uint32				  Subresource;
	};

	ScratchAllocator											m_ScratchAllocator;

	Ref<ID3D12GraphicsCommandListX>								m_pCommandList;
	Ref<ID3D12CommandAllocator>									m_pAllocator;

	Ref<ID3D12DescriptorHeap>									m_pRTVHeap;
	uint32														m_RTVSize = 0;
	Ref<ID3D12DescriptorHeap>									m_pDSVHeap;

	Array<D3D12_RESOURCE_BARRIER>								m_BatchedBarriers;
	Array<PendingBarrier>										m_PendingBarriers;
	HashMap<const DeviceResource*, ResourceState>				m_ResourceStates;

	D3D12_COMMAND_LIST_TYPE										m_Type;
	CommandListContext											m_CurrentCommandContext = CommandListContext::Invalid;
	using ResolveParams	= StaticArray<D3D12_RENDER_PASS_ENDING_ACCESS_RESOLVE_SUBRESOURCE_PARAMETERS, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT>;
	ResolveParams												m_ResolveSubResourceParameters{};
	RenderPassInfo												m_CurrentRenderPassInfo;
	bool														m_InRenderPass = false;

	const PipelineState*										m_pCurrentPSO		  = nullptr;
	const StateObject*											m_pCurrentSO		  = nullptr;
	const RootSignature*										m_pCurrentComputeRS  = nullptr;
	const RootSignature*										m_pCurrentGraphicsRS = nullptr;
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
