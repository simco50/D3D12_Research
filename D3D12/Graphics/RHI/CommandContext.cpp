#include "stdafx.h"
#include "CommandContext.h"
#include "Graphics.h"
#include "CommandQueue.h"
#include "DynamicResourceAllocator.h"
#include "GPUDescriptorHeap.h"
#include "PipelineState.h"
#include "RootSignature.h"
#include "Buffer.h"
#include "Texture.h"
#include "ResourceViews.h"
#include "ShaderBindingTable.h"
#include "StateObject.h"

CommandContext::CommandContext(GraphicsDevice* pParent, RefCountPtr<ID3D12CommandList> pCommandList, D3D12_COMMAND_LIST_TYPE type, GPUDescriptorHeap* pDescriptorHeap, DynamicAllocationManager* pDynamicMemoryManager)
	: GraphicsObject(pParent), m_ShaderResourceDescriptorAllocator(pDescriptorHeap), m_pCommandListBase(pCommandList), m_Type(type)
{
	m_pDynamicAllocator = std::make_unique<DynamicResourceAllocator>(pDynamicMemoryManager);
	pCommandList.As(&m_pCommandList);
	pCommandList.As(&m_pRaytracingCommandList);
	pCommandList.As(&m_pMeshShadingCommandList);
}

void CommandContext::Reset()
{
	check(m_pCommandList);
	if (m_pAllocator == nullptr)
	{
		m_pAllocator = GetParent()->GetCommandQueue(m_Type)->RequestAllocator();
		m_pCommandList->Reset(m_pAllocator, nullptr);
	}

	m_BarrierBatcher.Reset();
	m_PendingBarriers.clear();
	m_ResourceStates.clear();

	m_CurrentCommandContext = CommandListContext::Invalid;

	m_pCurrentPSO = nullptr;
	m_pCurrentSO = nullptr;

	if (m_Type != D3D12_COMMAND_LIST_TYPE_COPY)
	{
		ID3D12DescriptorHeap* pHeaps[] =
		{
			GetParent()->GetGlobalViewHeap()->GetHeap(),
			GetParent()->GetGlobalSamplerHeap()->GetHeap(),
		};
		m_pCommandList->SetDescriptorHeaps(ARRAYSIZE(pHeaps), pHeaps);
	}
}

SyncPoint CommandContext::Execute(bool wait)
{
	return Execute(this, wait);
}

SyncPoint CommandContext::Execute(const Span<CommandContext* const>& contexts, bool wait)
{
	check(contexts.GetSize() > 0);
	CommandQueue* pQueue = contexts[0]->GetParent()->GetCommandQueue(contexts[0]->GetType());
	for(CommandContext* pContext : contexts)
	{
		checkf(pContext->GetType() == pQueue->GetType(), "All commandlist types must match. Expected %s, got %s",
			D3D::CommandlistTypeToString(pQueue->GetType()), D3D::CommandlistTypeToString(pContext->GetType()));
		pContext->FlushResourceBarriers();
	}
	SyncPoint syncPoint = pQueue->ExecuteCommandLists(contexts, wait);
	for (CommandContext* pContext : contexts)
	{
		pContext->Free(syncPoint);
	}
	return syncPoint;
}

void CommandContext::Free(const SyncPoint& syncPoint)
{
	m_pDynamicAllocator->Free(syncPoint);
	GetParent()->GetCommandQueue(m_Type)->FreeAllocator(syncPoint, m_pAllocator);
	m_pAllocator = nullptr;
	GetParent()->FreeCommandList(this);

	if (m_Type != D3D12_COMMAND_LIST_TYPE_COPY)
	{
		m_ShaderResourceDescriptorAllocator.ReleaseUsedHeaps(syncPoint);
	}
}

bool NeedsTransition(D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES& after)
{
	//Can read from 'write' DSV
	if (before == D3D12_RESOURCE_STATE_DEPTH_WRITE && after == D3D12_RESOURCE_STATE_DEPTH_READ)
	{
		return false;
	}
	if (after == D3D12_RESOURCE_STATE_COMMON)
	{
		return before != D3D12_RESOURCE_STATE_COMMON;
	}
	//Combine already transitioned bits
	if (ResourceState::CanCombineResourceState(before, after) && !EnumHasAllFlags(before, after))
	{
		after |= before;
	}
	return before != after;
}

void CommandContext::InsertResourceBarrier(GraphicsResource* pBuffer, D3D12_RESOURCE_STATES state, uint32 subResource /*= D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES*/)
{
	check(pBuffer && pBuffer->GetResource());
	checkf(IsTransitionAllowed(m_Type, state), "After state (%s) is not valid on this commandlist type (%s)", D3D::ResourceStateToString(state).c_str(), D3D::CommandlistTypeToString(m_Type));

	ResourceState& resourceState = m_ResourceStates[pBuffer];
	D3D12_RESOURCE_STATES beforeState = resourceState.Get(subResource);
	if (beforeState == D3D12_RESOURCE_STATE_UNKNOWN)
	{
		resourceState.Set(state, subResource);

		PendingBarrier barrier;
		barrier.pResource = pBuffer;
		barrier.State = resourceState;
		barrier.Subresource = subResource;
		m_PendingBarriers.push_back(barrier);
	}
	else
	{
		if (NeedsTransition(beforeState, state))
		{
			checkf(IsTransitionAllowed(m_Type, beforeState), "Current resource state (%s) is not valid to transition from in this commandlist type (%s)", D3D::ResourceStateToString(state).c_str(), D3D::CommandlistTypeToString(m_Type));
			m_BarrierBatcher.AddTransition(pBuffer->GetResource(), beforeState, state, subResource);
			resourceState.Set(state, subResource);
		}
	}
}

void CommandContext::InsertUavBarrier(const GraphicsResource* pBuffer /*= nullptr*/)
{
	m_BarrierBatcher.AddUAV(pBuffer ? pBuffer->GetResource() : nullptr);
}

void CommandContext::FlushResourceBarriers()
{
	m_BarrierBatcher.Flush(m_pCommandList);
}

void CommandContext::CopyResource(const GraphicsResource* pSource, const GraphicsResource* pTarget)
{
	checkf(pSource && pSource->GetResource(), "Source is invalid");
	checkf(pTarget && pTarget->GetResource(), "Target is invalid");
	FlushResourceBarriers();
	m_pCommandList->CopyResource(pTarget->GetResource(), pSource->GetResource());
}

void CommandContext::CopyTexture(const Texture* pSource, const Buffer* pTarget, const D3D12_BOX& sourceRegion, uint32 sourceSubresource /*= 0*/, uint32 destinationOffset /*= 0*/)
{
	checkf(pSource && pSource->GetResource(), "Source is invalid");
	checkf(pTarget && pTarget->GetResource(), "Target is invalid");

	D3D12_PLACED_SUBRESOURCE_FOOTPRINT textureFootprint;
	textureFootprint.Offset = 0;
	textureFootprint.Footprint.Width = sourceRegion.right - sourceRegion.left;
	textureFootprint.Footprint.Depth = sourceRegion.back - sourceRegion.front;
	textureFootprint.Footprint.Height = sourceRegion.bottom - sourceRegion.top;
	textureFootprint.Footprint.Format = D3D::ConvertFormat(pSource->GetFormat());
	textureFootprint.Footprint.RowPitch = Math::AlignUp<uint32>(GetFormatByteSize(pSource->GetFormat(), textureFootprint.Footprint.Width), D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);

	CD3DX12_TEXTURE_COPY_LOCATION srcLocation(pSource->GetResource(), sourceSubresource);
	CD3DX12_TEXTURE_COPY_LOCATION dstLocation(pTarget->GetResource(), textureFootprint);
	FlushResourceBarriers();
	m_pCommandList->CopyTextureRegion(&dstLocation, destinationOffset, 0, 0, &srcLocation, &sourceRegion);
}

void CommandContext::CopyTexture(const Texture* pSource, const Texture* pTarget, const D3D12_BOX& sourceRegion, const D3D12_BOX& destinationRegion, uint32 sourceSubresource /*= 0*/, uint32 destinationSubregion /*= 0*/)
{
	checkf(pSource && pSource->GetResource(), "Source is invalid");
	checkf(pTarget && pTarget->GetResource(), "Target is invalid");
	CD3DX12_TEXTURE_COPY_LOCATION srcLocation(pSource->GetResource(), sourceSubresource);
	CD3DX12_TEXTURE_COPY_LOCATION dstLocation(pTarget->GetResource(), destinationSubregion);
	FlushResourceBarriers();
	m_pCommandList->CopyTextureRegion(&dstLocation, destinationRegion.left, destinationRegion.top, destinationRegion.front, &srcLocation, &sourceRegion);
}

void CommandContext::CopyBuffer(const Buffer* pSource, const Buffer* pTarget, uint64 size, uint64 sourceOffset, uint64 destinationOffset)
{
	checkf(pSource && pSource->GetResource(), "Source is invalid");
	checkf(pTarget && pTarget->GetResource(), "Target is invalid");
	FlushResourceBarriers();
	m_pCommandList->CopyBufferRegion(pTarget->GetResource(), destinationOffset, pSource->GetResource(), sourceOffset, size);
}

void CommandContext::WriteBuffer(const Buffer* pResource, const void* pData, uint64 dataSize, uint64 offset)
{
	DynamicAllocation allocation = m_pDynamicAllocator->Allocate(dataSize);
	memcpy(allocation.pMappedMemory, pData, dataSize);
	CopyBuffer(allocation.pBackingResource, pResource, dataSize, allocation.Offset, offset);
}

void CommandContext::WriteTexture(Texture* pResource, const Span<D3D12_SUBRESOURCE_DATA>& subResourceDatas, uint32 firstSubResource)
{
	FlushResourceBarriers();
	uint64 requiredSize = GetRequiredIntermediateSize(pResource->GetResource(), firstSubResource, subResourceDatas.GetSize());
	DynamicAllocation allocation = m_pDynamicAllocator->Allocate(requiredSize, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
	UpdateSubresources(m_pCommandList, pResource->GetResource(), allocation.pBackingResource->GetResource(), allocation.Offset, firstSubResource, subResourceDatas.GetSize(), subResourceDatas.GetData());
}

void CommandContext::Dispatch(uint32 groupCountX, uint32 groupCountY, uint32 groupCountZ)
{
	check(m_pCurrentPSO && m_pCurrentPSO->GetType() == PipelineStateType::Compute);
	check(m_CurrentCommandContext == CommandListContext::Compute);
	checkf(
		groupCountX <= D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION &&
		groupCountY <= D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION &&
		groupCountZ <= D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION,
		"Dispatch group size (%d x %d x %d) can not exceed %d", groupCountX, groupCountY, groupCountZ, D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION);
	PrepareDraw();
	m_pCommandList->Dispatch(groupCountX, groupCountY, groupCountZ);
}

void CommandContext::Dispatch(const Vector3i& groupCounts)
{
	Dispatch(groupCounts.x, groupCounts.y, groupCounts.z);
}

void CommandContext::DispatchMesh(uint32 groupCountX, uint32 groupCountY /*= 1*/, uint32 groupCountZ /*= 1*/)
{
	check(m_pCurrentPSO && m_pCurrentPSO->GetType() == PipelineStateType::Mesh);
	check(m_CurrentCommandContext == CommandListContext::Graphics);
	check(m_pMeshShadingCommandList);
	PrepareDraw();
	m_pMeshShadingCommandList->DispatchMesh(groupCountX, groupCountY, groupCountZ);
}

void CommandContext::DispatchMesh(const Vector3i& groupCounts)
{
	DispatchMesh(groupCounts.x, groupCounts.y, groupCounts.z);
}

void CommandContext::ExecuteIndirect(const CommandSignature* pCommandSignature, uint32 maxCount, const Buffer* pIndirectArguments, const Buffer* pCountBuffer, uint32 argumentsOffset /*= 0*/, uint32 countOffset /*= 0*/)
{
	PrepareDraw();
	check(m_pCurrentPSO || m_pCurrentSO);
	m_pCommandList->ExecuteIndirect(pCommandSignature->GetCommandSignature(), maxCount, pIndirectArguments->GetResource(), argumentsOffset, pCountBuffer ? pCountBuffer->GetResource() : nullptr, countOffset);
}

void CommandContext::ClearUAVu(const GraphicsResource* pBuffer, const UnorderedAccessView* pUav, const Vector4u& values)
{
	if (!pUav)
		pUav = pBuffer->GetUAV();
	check(pBuffer);
	check(pUav);

	DescriptorHandle gpuHandle = m_ShaderResourceDescriptorAllocator.Allocate(1);
	GetParent()->GetDevice()->CopyDescriptorsSimple(1, gpuHandle.CpuHandle, pUav->GetDescriptor(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	FlushResourceBarriers();
	m_pCommandList->ClearUnorderedAccessViewUint(gpuHandle.GpuHandle, pUav->GetDescriptor(), pBuffer->GetResource(), &values.x, 0, nullptr);
}

void CommandContext::ClearUAVf(const GraphicsResource* pBuffer, const UnorderedAccessView* pUav, const Vector4& values)
{
	if (!pUav)
		pUav = pBuffer->GetUAV();
	check(pBuffer);
	check(pUav);

	DescriptorHandle gpuHandle = m_ShaderResourceDescriptorAllocator.Allocate(1);
	GetParent()->GetDevice()->CopyDescriptorsSimple(1, gpuHandle.CpuHandle, pUav->GetDescriptor(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	FlushResourceBarriers();
	m_pCommandList->ClearUnorderedAccessViewFloat(gpuHandle.GpuHandle, pUav->GetDescriptor(), pBuffer->GetResource(), &values.x, 0, nullptr);
}

void CommandContext::SetComputeRootSignature(const RootSignature* pRootSignature)
{
	m_pCommandList->SetComputeRootSignature(pRootSignature->GetRootSignature());
	m_ShaderResourceDescriptorAllocator.ParseRootSignature(pRootSignature);
	m_CurrentCommandContext = CommandListContext::Compute;
}

void CommandContext::SetGraphicsRootSignature(const RootSignature* pRootSignature)
{
	m_pCommandList->SetGraphicsRootSignature(pRootSignature->GetRootSignature());
	m_ShaderResourceDescriptorAllocator.ParseRootSignature(pRootSignature);
	m_CurrentCommandContext = CommandListContext::Graphics;
}

void CommandContext::SetRootSRV(uint32 rootIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
{
	check(m_CurrentCommandContext != CommandListContext::Invalid);
	if (m_CurrentCommandContext == CommandListContext::Graphics)
	{
		m_pCommandList->SetGraphicsRootShaderResourceView(rootIndex, address);
	}
	else
	{
		m_pCommandList->SetComputeRootShaderResourceView(rootIndex, address);
	}
}

void CommandContext::SetRootUAV(uint32 rootIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
{
	check(m_CurrentCommandContext != CommandListContext::Invalid);
	if (m_CurrentCommandContext == CommandListContext::Graphics)
	{
		m_pCommandList->SetGraphicsRootUnorderedAccessView(rootIndex, address);
	}
	else
	{
		m_pCommandList->SetComputeRootUnorderedAccessView(rootIndex, address);
	}
}

void CommandContext::SetRootConstants(uint32 rootIndex, uint32 count, const void* pConstants)
{
	check(m_CurrentCommandContext != CommandListContext::Invalid);
	if (m_CurrentCommandContext == CommandListContext::Graphics)
	{
		m_pCommandList->SetGraphicsRoot32BitConstants(rootIndex, count, pConstants, 0);
	}
	else
	{
		m_pCommandList->SetComputeRoot32BitConstants(rootIndex, count, pConstants, 0);
	}
}

void CommandContext::SetRootCBV(uint32 rootIndex, const void* pData, uint32 dataSize)
{
	check(m_CurrentCommandContext != CommandListContext::Invalid);
	DynamicAllocation allocation = m_pDynamicAllocator->Allocate(dataSize);
	memcpy(allocation.pMappedMemory, pData, dataSize);

	if (m_CurrentCommandContext == CommandListContext::Graphics)
	{
		m_pCommandList->SetGraphicsRootConstantBufferView(rootIndex, allocation.GpuHandle);
	}
	else
	{
		m_pCommandList->SetComputeRootConstantBufferView(rootIndex, allocation.GpuHandle);
	}
}

void CommandContext::BindResources(uint32 rootIndex, const Span<const ResourceView*>& pViews, uint32 offset)
{
	std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 16> descriptors;
	check(pViews.GetSize() < descriptors.size());
	for (uint32 i = 0; i < pViews.GetSize(); ++i)
	{
		checkf(pViews[i], "ResourceView bound to root index %d with offset %d is null", rootIndex, offset);
		descriptors[i] = pViews[i]->GetDescriptor();
	}
	BindResources(rootIndex, Span<D3D12_CPU_DESCRIPTOR_HANDLE>(descriptors.data(), pViews.GetSize()), offset);
}

void CommandContext::BindResources(uint32 rootIndex, const Span<D3D12_CPU_DESCRIPTOR_HANDLE>& handles, uint32 offset)
{
	m_ShaderResourceDescriptorAllocator.SetDescriptors(rootIndex, offset, handles);
}

void CommandContext::SetShadingRate(D3D12_SHADING_RATE shadingRate /*= D3D12_SHADING_RATE_1X1*/)
{
	check(m_pMeshShadingCommandList);
	m_pMeshShadingCommandList->RSSetShadingRate(shadingRate, nullptr);
}

void CommandContext::SetShadingRateImage(Texture* pTexture)
{
	check(m_pMeshShadingCommandList);
	m_pMeshShadingCommandList->RSSetShadingRateImage(pTexture->GetResource());
}

DynamicAllocation CommandContext::AllocateTransientMemory(uint64 size, uint32 alignment /*= 256*/)
{
	return m_pDynamicAllocator->Allocate(size, alignment);
}

bool CommandContext::IsTransitionAllowed(D3D12_COMMAND_LIST_TYPE commandlistType, D3D12_RESOURCE_STATES state)
{
	constexpr int VALID_COMPUTE_QUEUE_RESOURCE_STATES =
		D3D12_RESOURCE_STATE_COMMON
		| D3D12_RESOURCE_STATE_UNORDERED_ACCESS
		| D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
		| D3D12_RESOURCE_STATE_COPY_DEST
		| D3D12_RESOURCE_STATE_COPY_SOURCE
		| D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;

	constexpr int VALID_COPY_QUEUE_RESOURCE_STATES =
		D3D12_RESOURCE_STATE_COMMON
		| D3D12_RESOURCE_STATE_COPY_DEST
		| D3D12_RESOURCE_STATE_COPY_SOURCE;

	if (commandlistType == D3D12_COMMAND_LIST_TYPE_COMPUTE)
	{
		return (state & VALID_COMPUTE_QUEUE_RESOURCE_STATES) == state;
	}
	else if (commandlistType == D3D12_COMMAND_LIST_TYPE_COPY)
	{
		return (state & VALID_COPY_QUEUE_RESOURCE_STATES) == state;
	}
	return true;
}

void CommandContext::ResolvePendingBarriers(CommandContext& resolveContext)
{
	for (const CommandContext::PendingBarrier& pending : m_PendingBarriers)
	{
		uint32 subResource = pending.Subresource;
		GraphicsResource* pResource = pending.pResource;
		D3D12_RESOURCE_STATES beforeState = pResource->GetResourceState(subResource);
		checkf(CommandContext::IsTransitionAllowed(m_Type, beforeState),
			"Resource (%s) can not be transitioned from this state (%s) on this queue (%s). Insert a barrier on another queue before executing this one.",
			pResource->GetName().c_str(), D3D::ResourceStateToString(beforeState).c_str(), D3D::CommandlistTypeToString(m_Type));

		resolveContext.m_BarrierBatcher.AddTransition(pResource->GetResource(), beforeState, pending.State.Get(subResource), subResource);
		pResource->SetResourceState(GetLocalResourceState(pending.pResource, subResource));
	}
	resolveContext.FlushResourceBarriers();
}

void CommandContext::BeginRenderPass(const RenderPassInfo& renderPassInfo)
{
	checkf(!m_InRenderPass, "Already in RenderPass");
	checkf(renderPassInfo.DepthStencilTarget.Target
		|| (renderPassInfo.DepthStencilTarget.Access == RenderPassAccess::NoAccess && renderPassInfo.DepthStencilTarget.StencilAccess == RenderPassAccess::NoAccess),
		"Either a depth texture must be assigned or the access should be 'NoAccess'");
	auto ExtractBeginAccess = [](RenderPassAccess access)
	{
		switch (RenderPassInfo::GetBeginAccess(access))
		{
		case RenderTargetLoadAction::DontCare: return D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_DISCARD;
		case RenderTargetLoadAction::Load: return D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE;
		case RenderTargetLoadAction::Clear: return D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
		case RenderTargetLoadAction::NoAccess: return D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_NO_ACCESS;
		}
		noEntry();
		return D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_NO_ACCESS;
	};

	auto ExtractEndingAccess = [](RenderPassAccess access)
	{
		switch (RenderPassInfo::GetEndAccess(access))
		{
		case RenderTargetStoreAction::DontCare: return D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_DISCARD;
		case RenderTargetStoreAction::Store: return D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;
		case RenderTargetStoreAction::Resolve: return D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_RESOLVE;
		case RenderTargetStoreAction::NoAccess: return D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_NO_ACCESS;
		}
		noEntry();
		return D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_NO_ACCESS;
	};

	D3D12_RENDER_PASS_DEPTH_STENCIL_DESC renderPassDepthStencilDesc{};
	renderPassDepthStencilDesc.DepthBeginningAccess.Type = ExtractBeginAccess(renderPassInfo.DepthStencilTarget.Access);
	if (renderPassDepthStencilDesc.DepthBeginningAccess.Type == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR)
	{
		check(renderPassInfo.DepthStencilTarget.Target);
		check(renderPassInfo.DepthStencilTarget.Target->GetClearBinding().BindingValue == ClearBinding::ClearBindingValue::DepthStencil);
		renderPassDepthStencilDesc.DepthBeginningAccess.Clear.ClearValue.DepthStencil.Depth = renderPassInfo.DepthStencilTarget.Target->GetClearBinding().DepthStencil.Depth;
		renderPassDepthStencilDesc.DepthBeginningAccess.Clear.ClearValue.Format = D3D::ConvertFormat(renderPassInfo.DepthStencilTarget.Target->GetFormat());
	}
	renderPassDepthStencilDesc.DepthEndingAccess.Type = ExtractEndingAccess(renderPassInfo.DepthStencilTarget.Access);
	if (renderPassDepthStencilDesc.DepthEndingAccess.Type == D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_DISCARD)
	{
		check(renderPassInfo.DepthStencilTarget.Write == false);
	}
	renderPassDepthStencilDesc.StencilBeginningAccess.Type = ExtractBeginAccess(renderPassInfo.DepthStencilTarget.StencilAccess);
	if (renderPassDepthStencilDesc.StencilBeginningAccess.Type == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR)
	{
		check(renderPassInfo.DepthStencilTarget.Target);
		check(renderPassInfo.DepthStencilTarget.Target->GetClearBinding().BindingValue == ClearBinding::ClearBindingValue::DepthStencil);
		renderPassDepthStencilDesc.StencilBeginningAccess.Clear.ClearValue.DepthStencil.Stencil = renderPassInfo.DepthStencilTarget.Target->GetClearBinding().DepthStencil.Stencil;
		renderPassDepthStencilDesc.StencilBeginningAccess.Clear.ClearValue.Format = D3D::ConvertFormat(renderPassInfo.DepthStencilTarget.Target->GetFormat());
	}
	renderPassDepthStencilDesc.StencilEndingAccess.Type = ExtractEndingAccess(renderPassInfo.DepthStencilTarget.StencilAccess);
	if (renderPassInfo.DepthStencilTarget.Target)
	{
		renderPassDepthStencilDesc.cpuDescriptor = renderPassInfo.DepthStencilTarget.Target->GetDSV(renderPassInfo.DepthStencilTarget.Write);
	}

	std::array<D3D12_RENDER_PASS_RENDER_TARGET_DESC, 4> renderTargetDescs{};
	m_ResolveSubResourceParameters = {};
	for (uint32 i = 0; i < renderPassInfo.RenderTargetCount; ++i)
	{
		const RenderPassInfo::RenderTargetInfo& data = renderPassInfo.RenderTargets[i];

		renderTargetDescs[i].BeginningAccess.Type = ExtractBeginAccess(data.Access);

		if (renderTargetDescs[i].BeginningAccess.Type == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR)
		{
			check(data.Target->GetClearBinding().BindingValue == ClearBinding::ClearBindingValue::Color);
			const Color& clearColor = data.Target->GetClearBinding().Color;
			D3D12_CLEAR_VALUE& clearValue = renderTargetDescs[i].BeginningAccess.Clear.ClearValue;
			clearValue.Color[0] = clearColor.x;
			clearValue.Color[1] = clearColor.y;
			clearValue.Color[2] = clearColor.z;
			clearValue.Color[3] = clearColor.w;
			clearValue.Format = D3D::ConvertFormat(data.Target->GetFormat());
		}

		D3D12_RENDER_PASS_ENDING_ACCESS_TYPE endingAccess = ExtractEndingAccess(data.Access);
		if (data.Target->GetDesc().SampleCount <= 1 && endingAccess == D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_RESOLVE)
		{
			validateOncef(data.Target == data.ResolveTarget, "RenderTarget %d is set to resolve but has a sample count of 1. This will just do a CopyTexture instead which is wasteful.", i);
			endingAccess = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;
		}
		renderTargetDescs[i].EndingAccess.Type = endingAccess;

		uint32 subResource = D3D12CalcSubresource(data.MipLevel, data.ArrayIndex, 0, data.Target->GetMipLevels(), data.Target->GetArraySize());

		if (renderTargetDescs[i].EndingAccess.Type == D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_RESOLVE)
		{
			checkf(data.ResolveTarget, "Expected ResolveTarget because ending access is 'Resolve'");
			InsertResourceBarrier(data.ResolveTarget, D3D12_RESOURCE_STATE_RESOLVE_DEST);
			renderTargetDescs[i].EndingAccess.Resolve.Format = D3D::ConvertFormat(data.Target->GetFormat());
			renderTargetDescs[i].EndingAccess.Resolve.pDstResource = data.ResolveTarget->GetResource();
			renderTargetDescs[i].EndingAccess.Resolve.pSrcResource = data.Target->GetResource();
			renderTargetDescs[i].EndingAccess.Resolve.PreserveResolveSource = false;
			renderTargetDescs[i].EndingAccess.Resolve.ResolveMode = D3D12_RESOLVE_MODE_AVERAGE;
			renderTargetDescs[i].EndingAccess.Resolve.SubresourceCount = 1;
			m_ResolveSubResourceParameters[i].DstSubresource = 0;
			m_ResolveSubResourceParameters[i].SrcSubresource = subResource;
			m_ResolveSubResourceParameters[i].DstX = 0;
			m_ResolveSubResourceParameters[i].DstY = 0;
			renderTargetDescs[i].EndingAccess.Resolve.pSubresourceParameters = m_ResolveSubResourceParameters.data();
		}

		renderTargetDescs[i].cpuDescriptor = data.Target->GetRTV();
	}

	D3D12_RENDER_PASS_FLAGS renderPassFlags = D3D12_RENDER_PASS_FLAG_NONE;
	if (renderPassInfo.WriteUAVs)
	{
		renderPassFlags |= D3D12_RENDER_PASS_FLAG_ALLOW_UAV_WRITES;
	}

	FlushResourceBarriers();
	m_pRaytracingCommandList->BeginRenderPass(renderPassInfo.RenderTargetCount, renderTargetDescs.data(), renderPassInfo.DepthStencilTarget.Target ? &renderPassDepthStencilDesc : nullptr, renderPassFlags);

	m_InRenderPass = true;
	m_CurrentRenderPassInfo = renderPassInfo;

	Texture* pTargetTexture = renderPassInfo.DepthStencilTarget.Target ? renderPassInfo.DepthStencilTarget.Target : renderPassInfo.RenderTargets[0].Target;
	SetViewport(FloatRect(0, 0, (float)pTargetTexture->GetWidth(), (float)pTargetTexture->GetHeight()), 0, 1);
}

void CommandContext::EndRenderPass()
{
	check(m_InRenderPass);

	auto ExtractEndingAccess = [](RenderPassAccess access) -> D3D12_RENDER_PASS_ENDING_ACCESS_TYPE
	{
		switch (RenderPassInfo::GetEndAccess(access))
		{
		case RenderTargetStoreAction::DontCare: return D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_DISCARD;
		case RenderTargetStoreAction::Store: return D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;
		case RenderTargetStoreAction::Resolve: return D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_RESOLVE;
		case RenderTargetStoreAction::NoAccess: return D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_NO_ACCESS;
		}
		noEntry();
		return D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_NO_ACCESS;
	};

	m_pRaytracingCommandList->EndRenderPass();

	for (uint32 i = 0; i < m_CurrentRenderPassInfo.RenderTargetCount; ++i)
	{
		const RenderPassInfo::RenderTargetInfo& data = m_CurrentRenderPassInfo.RenderTargets[i];
		if (ExtractEndingAccess(data.Access) == D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_RESOLVE && data.Target->GetDesc().SampleCount <= 1 && data.Target != data.ResolveTarget)
		{
			FlushResourceBarriers();
			CopyResource(data.Target, data.ResolveTarget);
		}
	}

	m_InRenderPass = false;
}

void CommandContext::Draw(uint32 vertexStart, uint32 vertexCount, uint32 instances, uint32 instanceStart)
{
	check(m_pCurrentPSO && m_pCurrentPSO->GetType() == PipelineStateType::Graphics);
	check(m_CurrentCommandContext == CommandListContext::Graphics);
	PrepareDraw();
	m_pCommandList->DrawInstanced(vertexCount, instances, vertexStart, instanceStart);
}

void CommandContext::DrawIndexedInstanced(uint32 indexCount, uint32 indexStart, uint32 instanceCount, uint32 minVertex /*= 0*/, uint32 instanceStart /*= 0*/)
{
	check(m_pCurrentPSO && m_pCurrentPSO->GetType() == PipelineStateType::Graphics);
	check(m_CurrentCommandContext == CommandListContext::Graphics);
	PrepareDraw();
	m_pCommandList->DrawIndexedInstanced(indexCount, instanceCount, indexStart, minVertex, instanceStart);
}

void CommandContext::DispatchRays(ShaderBindingTable& table, uint32 width /*= 1*/, uint32 height /*= 1*/, uint32 depth /*= 1*/)
{
	check(m_pCurrentSO);
	check(m_CurrentCommandContext == CommandListContext::Compute);
	check(m_pRaytracingCommandList);
	D3D12_DISPATCH_RAYS_DESC desc{};
	table.Commit(*this, desc);
	desc.Width = width;
	desc.Height = height;
	desc.Depth = depth;
	PrepareDraw();
	m_pRaytracingCommandList->DispatchRays(&desc);
}

void CommandContext::ClearColor(D3D12_CPU_DESCRIPTOR_HANDLE rtv, const Color& color /*= Color(0.15f, 0.15f, 0.15f, 1.0f)*/)
{
	m_pCommandList->ClearRenderTargetView(rtv, &color.x, 0, nullptr);
}

void CommandContext::ClearDepth(D3D12_CPU_DESCRIPTOR_HANDLE dsv, D3D12_CLEAR_FLAGS clearFlags /*= D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL*/, float depth /*= 1.0f*/, unsigned char stencil /*= 0*/)
{
	m_pCommandList->ClearDepthStencilView(dsv, clearFlags, depth, stencil, 0, nullptr);
}

void CommandContext::ResolveResource(Texture* pSource, uint32 sourceSubResource, Texture* pTarget, uint32 targetSubResource, ResourceFormat format)
{
	FlushResourceBarriers();
	m_pCommandList->ResolveSubresource(pTarget->GetResource(), targetSubResource, pSource->GetResource(), sourceSubResource, D3D::ConvertFormat(format));
}

void CommandContext::PrepareDraw()
{
	check(m_CurrentCommandContext != CommandListContext::Invalid);
	FlushResourceBarriers();
	m_ShaderResourceDescriptorAllocator.BindStagedDescriptors(*this, m_CurrentCommandContext);
}

void CommandContext::SetPipelineState(PipelineState* pPipelineState)
{
	if (m_pCurrentPSO != pPipelineState)
	{
		pPipelineState->ConditionallyReload();
		m_pCommandList->SetPipelineState(pPipelineState->GetPipelineState());
		m_pCurrentPSO = pPipelineState;
	}
}

void CommandContext::SetPipelineState(StateObject* pStateObject)
{
	check(m_pRaytracingCommandList);
	if (m_pCurrentSO != pStateObject)
	{
		pStateObject->ConditionallyReload();
		m_pRaytracingCommandList->SetPipelineState1(pStateObject->GetStateObject());
		m_pCurrentSO = pStateObject;
	}
}

void CommandContext::SetDynamicVertexBuffer(uint32 rootIndex, uint32 elementCount, uint32 elementSize, const void* pData)
{
	uint32 bufferSize = elementCount * elementSize;
	DynamicAllocation allocation = m_pDynamicAllocator->Allocate(bufferSize);
	memcpy(allocation.pMappedMemory, pData, bufferSize);
	D3D12_VERTEX_BUFFER_VIEW view = {};
	view.BufferLocation = allocation.GpuHandle;
	view.SizeInBytes = bufferSize;
	view.StrideInBytes = elementSize;
	m_pCommandList->IASetVertexBuffers(rootIndex, 1, &view);
}

void CommandContext::SetDynamicIndexBuffer(uint32 elementCount, const void* pData, bool smallIndices /*= false*/)
{
	uint32 stride = smallIndices ? sizeof(uint16) : sizeof(uint32);
	uint32 bufferSize = elementCount * stride;
	DynamicAllocation allocation = m_pDynamicAllocator->Allocate(bufferSize);
	memcpy(allocation.pMappedMemory, pData, bufferSize);
	D3D12_INDEX_BUFFER_VIEW view = {};
	view.BufferLocation = allocation.GpuHandle;
	view.SizeInBytes = bufferSize;
	view.Format = smallIndices ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
	m_pCommandList->IASetIndexBuffer(&view);
}

void CommandContext::SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY type)
{
	m_pCommandList->IASetPrimitiveTopology(type);
}

void CommandContext::SetVertexBuffers(const Span<VertexBufferView>& buffers)
{
	constexpr uint32 MAX_VERTEX_BUFFERS = 4;
	checkf(buffers.GetSize() < MAX_VERTEX_BUFFERS, "VertexBuffer count (%d) exceeds the maximum (%d)", buffers.GetSize(), MAX_VERTEX_BUFFERS);
	std::array<D3D12_VERTEX_BUFFER_VIEW, MAX_VERTEX_BUFFERS> views = {};

	uint32 numViews = 0;
	for(const VertexBufferView& view : buffers)
	{
		views[numViews].BufferLocation = view.Location;
		views[numViews].SizeInBytes = view.Elements * view.Stride;
		views[numViews].StrideInBytes = view.Stride;
		++numViews;
	}
	m_pCommandList->IASetVertexBuffers(0, buffers.GetSize(), views.data());
}

void CommandContext::SetIndexBuffer(const IndexBufferView& indexBuffer)
{
	D3D12_INDEX_BUFFER_VIEW view;
	view.BufferLocation = indexBuffer.Location;
	view.Format = D3D::ConvertFormat(indexBuffer.Format);
	view.SizeInBytes = indexBuffer.Stride() * indexBuffer.Elements;
	m_pCommandList->IASetIndexBuffer(&view);
}

void CommandContext::SetViewport(const FloatRect& rect, float minDepth /*= 0.0f*/, float maxDepth /*= 1.0f*/)
{
	D3D12_VIEWPORT viewport;
	viewport.TopLeftX = (float)rect.Left;
	viewport.TopLeftY = (float)rect.Top;
	viewport.Height = (float)rect.GetHeight();
	viewport.Width = (float)rect.GetWidth();
	viewport.MinDepth = minDepth;
	viewport.MaxDepth = maxDepth;
	m_pCommandList->RSSetViewports(1, &viewport);
	SetScissorRect(rect);
}

void CommandContext::SetScissorRect(const FloatRect& rect)
{
	D3D12_RECT r;
	r.left = (LONG)rect.Left;
	r.top = (LONG)rect.Top;
	r.right = (LONG)rect.Right;
	r.bottom = (LONG)rect.Bottom;
	m_pCommandList->RSSetScissorRects(1, &r);
}

void ResourceBarrierBatcher::AddTransition(ID3D12Resource* pResource, D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState, uint32 subResource)
{
	if (beforeState == afterState)
	{
		return;
	}
	if (m_QueuedBarriers.size())
	{
		const D3D12_RESOURCE_BARRIER& last = m_QueuedBarriers.back();
		if (last.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION
			&& last.Transition.pResource == pResource
			&& last.Transition.StateBefore == beforeState
			&& last.Transition.StateAfter == afterState)
		{
			m_QueuedBarriers.pop_back();
			return;
		}
	}
	m_QueuedBarriers.emplace_back(
		CD3DX12_RESOURCE_BARRIER::Transition(pResource,
			beforeState,
			afterState,
			subResource,
			D3D12_RESOURCE_BARRIER_FLAG_NONE
		)
	);
}

void ResourceBarrierBatcher::AddUAV(ID3D12Resource* pResource)
{
	m_QueuedBarriers.emplace_back(
		CD3DX12_RESOURCE_BARRIER::UAV(pResource)
	);
}

void ResourceBarrierBatcher::Flush(ID3D12GraphicsCommandList* pCmdList)
{
	if (m_QueuedBarriers.size())
	{
		pCmdList->ResourceBarrier((uint32)m_QueuedBarriers.size(), m_QueuedBarriers.data());
		Reset();
	}
}

void ResourceBarrierBatcher::Reset()
{
	m_QueuedBarriers.clear();
}

CommandSignature::CommandSignature(GraphicsDevice* pParent, ID3D12CommandSignature* pCmdSignature)
	: GraphicsObject(pParent), m_pCommandSignature(pCmdSignature)
{
}

D3D12_COMMAND_SIGNATURE_DESC CommandSignatureInitializer::GetDesc() const
{
	D3D12_COMMAND_SIGNATURE_DESC desc;
	desc.ByteStride = m_Stride;
	desc.NodeMask = 0;
	desc.NumArgumentDescs = (uint32)m_ArgumentDesc.size();
	desc.pArgumentDescs = m_ArgumentDesc.data();
	return desc;
}

void CommandSignatureInitializer::AddDispatch()
{
	D3D12_INDIRECT_ARGUMENT_DESC& desc = m_ArgumentDesc.emplace_back();
	desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
	m_Stride += sizeof(D3D12_DISPATCH_ARGUMENTS);
}

void CommandSignatureInitializer::AddDispatchMesh()
{
	D3D12_INDIRECT_ARGUMENT_DESC& desc = m_ArgumentDesc.emplace_back();
	desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH;
	m_Stride += sizeof(D3D12_DISPATCH_MESH_ARGUMENTS);
}

void CommandSignatureInitializer::AddDraw()
{
	D3D12_INDIRECT_ARGUMENT_DESC& desc = m_ArgumentDesc.emplace_back();
	desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
	m_Stride += sizeof(D3D12_DRAW_ARGUMENTS);
}

void CommandSignatureInitializer::AddDrawIndexed()
{
	D3D12_INDIRECT_ARGUMENT_DESC& desc = m_ArgumentDesc.emplace_back();
	desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
	m_Stride += sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
}

void CommandSignatureInitializer::AddConstants(uint32 numConstants, uint32 rootIndex, uint32 offset)
{
	D3D12_INDIRECT_ARGUMENT_DESC& desc = m_ArgumentDesc.emplace_back();
	desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
	desc.Constant.RootParameterIndex = rootIndex;
	desc.Constant.DestOffsetIn32BitValues = offset;
	desc.Constant.Num32BitValuesToSet = numConstants;
	m_Stride += numConstants * sizeof(uint32);
}

void CommandSignatureInitializer::AddConstantBufferView(uint32 rootIndex)
{
	D3D12_INDIRECT_ARGUMENT_DESC& desc = m_ArgumentDesc.emplace_back();
	desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW;
	desc.ConstantBufferView.RootParameterIndex = rootIndex;
	m_Stride += sizeof(uint64);
}

void CommandSignatureInitializer::AddShaderResourceView(uint32 rootIndex)
{
	D3D12_INDIRECT_ARGUMENT_DESC& desc = m_ArgumentDesc.emplace_back();
	desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW;
	desc.ShaderResourceView.RootParameterIndex = rootIndex;
	m_Stride += 8;
}

void CommandSignatureInitializer::AddUnorderedAccessView(uint32 rootIndex)
{
	D3D12_INDIRECT_ARGUMENT_DESC& desc = m_ArgumentDesc.emplace_back();
	desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW;
	desc.UnorderedAccessView.RootParameterIndex = rootIndex;
	m_Stride += 8;
}

void CommandSignatureInitializer::AddVertexBuffer(uint32 slot)
{
	D3D12_INDIRECT_ARGUMENT_DESC& desc = m_ArgumentDesc.emplace_back();
	desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW;
	desc.VertexBuffer.Slot = slot;
	m_Stride += sizeof(D3D12_VERTEX_BUFFER_VIEW);
}

void CommandSignatureInitializer::AddIndexBuffer()
{
	D3D12_INDIRECT_ARGUMENT_DESC& desc = m_ArgumentDesc.emplace_back();
	desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW;
	m_Stride += sizeof(D3D12_INDEX_BUFFER_VIEW);
}
