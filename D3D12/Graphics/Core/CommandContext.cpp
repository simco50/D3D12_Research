#include "stdafx.h"
#include "CommandContext.h"
#include "Graphics.h"
#include "CommandQueue.h"
#include "DynamicResourceAllocator.h"
#include "OnlineDescriptorAllocator.h"
#include "PipelineState.h"
#include "RootSignature.h"
#include "Buffer.h"
#include "Texture.h"
#include "ResourceViews.h"
#include "ShaderBindingTable.h"
#include "StateObject.h"

CommandContext::CommandContext(GraphicsDevice* pParent, ID3D12GraphicsCommandList* pCommandList, D3D12_COMMAND_LIST_TYPE type, GlobalOnlineDescriptorHeap* pDescriptorHeap, DynamicAllocationManager* pDynamicMemoryManager, ID3D12CommandAllocator* pAllocator)
	: GraphicsObject(pParent), m_ShaderResourceDescriptorAllocator(pDescriptorHeap), m_pCommandList(pCommandList), m_pAllocator(pAllocator), m_Type(type)
{
	m_DynamicAllocator = std::make_unique<DynamicResourceAllocator>(pDynamicMemoryManager);
	pCommandList->QueryInterface(IID_PPV_ARGS(m_pRaytracingCommandList.GetAddressOf()));
	pCommandList->QueryInterface(IID_PPV_ARGS(m_pMeshShadingCommandList.GetAddressOf()));
}

CommandContext::~CommandContext()
{

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

uint64 CommandContext::Execute(bool wait)
{
	CommandContext* pContexts[] = { this };
	return Execute(pContexts, ARRAYSIZE(pContexts), wait);
}

uint64 CommandContext::Execute(CommandContext** pContexts, uint32 numContexts, bool wait)
{
	check(numContexts > 0);
	CommandQueue* pQueue = pContexts[0]->GetParent()->GetCommandQueue(pContexts[0]->GetType());
	for (uint32 i = 0; i < numContexts; ++i)
	{
		checkf(pContexts[i]->GetType() == pQueue->GetType(), "All commandlist types must match. Expected %s, got %s",
			D3D::CommandlistTypeToString(pQueue->GetType()), D3D::CommandlistTypeToString(pContexts[i]->GetType()));
		pContexts[i]->FlushResourceBarriers();
	}
	uint64 fenceValue = pQueue->ExecuteCommandLists(pContexts, numContexts);
	if (wait)
	{
		pQueue->WaitForFence(fenceValue);
	}

	for (uint32 i = 0; i < numContexts; ++i)
	{
		pContexts[i]->Free(fenceValue);
	}
	return fenceValue;
}

void CommandContext::Free(uint64 fenceValue)
{
	m_DynamicAllocator->Free(fenceValue);
	GetParent()->GetCommandQueue(m_Type)->FreeAllocator(fenceValue, m_pAllocator);
	m_pAllocator = nullptr;
	GetParent()->FreeCommandList(this);

	if (m_Type != D3D12_COMMAND_LIST_TYPE_COPY)
	{
		m_ShaderResourceDescriptorAllocator.ReleaseUsedHeaps(fenceValue);
	}
}

bool NeedsTransition(D3D12_RESOURCE_STATES& before, D3D12_RESOURCE_STATES& after)
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
	D3D12_RESOURCE_STATES combined = before | after;
	if ((combined & (D3D12_RESOURCE_STATE_GENERIC_READ | D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT)) == combined)
	{
		after = combined;
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
		PendingBarrier barrier;
		barrier.pResource = pBuffer;
		barrier.State = state;
		barrier.Subresource = subResource;
		m_PendingBarriers.push_back(barrier);
		resourceState.Set(state, subResource);
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

void CommandContext::InsertUavBarrier(GraphicsResource* pBuffer /*= nullptr*/)
{
	m_BarrierBatcher.AddUAV(pBuffer ? pBuffer->GetResource() : nullptr);
}

void CommandContext::FlushResourceBarriers()
{
	m_BarrierBatcher.Flush(m_pCommandList);
}

void CommandContext::CopyTexture(GraphicsResource* pSource, GraphicsResource* pTarget)
{
	checkf(pSource && pSource->GetResource(), "Source is invalid");
	checkf(pTarget && pTarget->GetResource(), "Target is invalid");
	InsertResourceBarrier(pSource, D3D12_RESOURCE_STATE_COPY_SOURCE);
	InsertResourceBarrier(pTarget, D3D12_RESOURCE_STATE_COPY_DEST);
	FlushResourceBarriers();
	m_pCommandList->CopyResource(pTarget->GetResource(), pSource->GetResource());
}

void CommandContext::CopyTexture(Texture* pSource, Buffer* pDestination, const D3D12_BOX& sourceRegion, int sourceSubregion /*= 0*/, int destinationOffset /*= 0*/)
{
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT textureFootprint = {};
	D3D12_RESOURCE_DESC resourceDesc = pSource->GetResource()->GetDesc();
	GetParent()->GetDevice()->GetCopyableFootprints(&resourceDesc, 0, 1, 0, &textureFootprint, nullptr, nullptr, nullptr);

	CD3DX12_TEXTURE_COPY_LOCATION srcLocation(pSource->GetResource(), sourceSubregion);
	CD3DX12_TEXTURE_COPY_LOCATION dstLocation(pDestination->GetResource(), textureFootprint);
	m_pCommandList->CopyTextureRegion(&dstLocation, destinationOffset, 0, 0, &srcLocation, &sourceRegion);
}

void CommandContext::CopyTexture(Texture* pSource, Texture* pDestination, const D3D12_BOX& sourceRegion, const D3D12_BOX& destinationRegion, int sourceSubregion /*= 0*/, int destinationSubregion /*= 0*/)
{
	CD3DX12_TEXTURE_COPY_LOCATION srcLocation(pSource->GetResource(), sourceSubregion);
	CD3DX12_TEXTURE_COPY_LOCATION dstLocation(pDestination->GetResource(), destinationSubregion);
	m_pCommandList->CopyTextureRegion(&dstLocation, destinationRegion.left, destinationRegion.top, destinationRegion.front, &srcLocation, &sourceRegion);
}

void CommandContext::CopyBuffer(Buffer* pSource, Buffer* pDestination, uint64 size, uint64 sourceOffset, uint64 destinationOffset)
{
	m_pCommandList->CopyBufferRegion(pDestination->GetResource(), destinationOffset, pSource->GetResource(), sourceOffset, size);
}

void CommandContext::InitializeBuffer(Buffer* pResource, const void* pData, uint64 dataSize, uint64 offset)
{
	DynamicAllocation allocation = m_DynamicAllocator->Allocate(dataSize);
	memcpy(allocation.pMappedMemory, pData, dataSize);
	CopyBuffer(allocation.pBackingResource, pResource, dataSize, allocation.Offset, offset);
}

void CommandContext::InitializeTexture(Texture* pResource, D3D12_SUBRESOURCE_DATA* pSubResourceDatas, int firstSubResource, int subResourceCount)
{
	uint64 requiredSize = GetRequiredIntermediateSize(pResource->GetResource(), firstSubResource, subResourceCount);
	DynamicAllocation allocation = m_DynamicAllocator->Allocate(requiredSize, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
	
	bool resetState = false;
	D3D12_RESOURCE_STATES previousState = GetResourceStateWithFallback(pResource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
	if (previousState != D3D12_RESOURCE_STATE_COPY_DEST)
	{
		resetState = true;
		InsertResourceBarrier(pResource, D3D12_RESOURCE_STATE_COPY_DEST);
		FlushResourceBarriers();
	}
	UpdateSubresources(m_pCommandList, pResource->GetResource(), allocation.pBackingResource->GetResource(), allocation.Offset, firstSubResource, subResourceCount, pSubResourceDatas);
	if (resetState)
	{
		InsertResourceBarrier(pResource, previousState);
	}
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

void CommandContext::Dispatch(const IntVector3& groupCounts)
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

void CommandContext::DispatchMesh(const IntVector3& groupCounts)
{
	DispatchMesh(groupCounts.x, groupCounts.y, groupCounts.z);
}

void CommandContext::ExecuteIndirect(CommandSignature* pCommandSignature, uint32 maxCount, Buffer* pIndirectArguments, Buffer* pCountBuffer, uint32 argumentsOffset /*= 0*/, uint32 countOffset /*= 0*/)
{
	PrepareDraw();
	check(m_pCurrentPSO || m_pCurrentSO);
	m_pCommandList->ExecuteIndirect(pCommandSignature->GetCommandSignature(), maxCount, pIndirectArguments->GetResource(), argumentsOffset, pCountBuffer ? pCountBuffer->GetResource() : nullptr, countOffset);
}

void CommandContext::ClearUavUInt(GraphicsResource* pBuffer, UnorderedAccessView* pUav, uint32* values /*= nullptr*/)
{
	FlushResourceBarriers();
	DescriptorHandle gpuHandle = m_ShaderResourceDescriptorAllocator.Allocate(1);
	GetParent()->GetDevice()->CopyDescriptorsSimple(1, gpuHandle.CpuHandle, pUav->GetDescriptor(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	uint32 zeros[4] = { 0,0,0,0 };
	m_pCommandList->ClearUnorderedAccessViewUint(gpuHandle.GpuHandle, pUav->GetDescriptor(), pBuffer->GetResource(), values ? values : zeros, 0, nullptr);
}

void CommandContext::ClearUavFloat(GraphicsResource* pBuffer, UnorderedAccessView* pUav, float* values /*= nullptr*/)
{
	FlushResourceBarriers();
	DescriptorHandle gpuHandle = m_ShaderResourceDescriptorAllocator.Allocate(1);
	GetParent()->GetDevice()->CopyDescriptorsSimple(1, gpuHandle.CpuHandle, pUav->GetDescriptor(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	float zeros[4] = { 0,0,0,0 };
	m_pCommandList->ClearUnorderedAccessViewFloat(gpuHandle.GpuHandle, pUav->GetDescriptor(), pBuffer->GetResource(), values ? values : zeros, 0, nullptr);
}

void CommandContext::SetComputeRootSignature(RootSignature* pRootSignature)
{
	m_pCommandList->SetComputeRootSignature(pRootSignature->GetRootSignature());
	m_ShaderResourceDescriptorAllocator.ParseRootSignature(pRootSignature);
	m_CurrentCommandContext = CommandListContext::Compute;

	m_pCommandList->SetComputeRootDescriptorTable(pRootSignature->GetBindlessViewIndex(), GetParent()->GetGlobalViewHeap()->GetStartHandle().GpuHandle);
	m_pCommandList->SetComputeRootDescriptorTable(pRootSignature->GetBindlessSamplerIndex(), GetParent()->GetGlobalSamplerHeap()->GetStartHandle().GpuHandle);
}

void CommandContext::SetGraphicsRootSignature(RootSignature* pRootSignature)
{
	m_pCommandList->SetGraphicsRootSignature(pRootSignature->GetRootSignature());
	m_ShaderResourceDescriptorAllocator.ParseRootSignature(pRootSignature);
	m_CurrentCommandContext = CommandListContext::Graphics;

	m_pCommandList->SetGraphicsRootDescriptorTable(pRootSignature->GetBindlessViewIndex(), GetParent()->GetGlobalViewHeap()->GetStartHandle().GpuHandle);
	m_pCommandList->SetGraphicsRootDescriptorTable(pRootSignature->GetBindlessSamplerIndex(), GetParent()->GetGlobalSamplerHeap()->GetStartHandle().GpuHandle);
}

void CommandContext::SetRootSRV(int rootIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
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

void CommandContext::SetRootUAV(int rootIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
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

void CommandContext::SetRootConstants(int rootIndex, uint32 count, const void* pConstants)
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

void CommandContext::SetRootCBV(int rootIndex, const void* pData, uint32 dataSize)
{
	check(m_CurrentCommandContext != CommandListContext::Invalid);
	DynamicAllocation allocation = m_DynamicAllocator->Allocate(dataSize);
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

void CommandContext::BindResource(int rootIndex, int offset, ResourceView* pView)
{
	D3D12_CPU_DESCRIPTOR_HANDLE handle = pView->GetDescriptor();
	m_ShaderResourceDescriptorAllocator.SetDescriptors(rootIndex, offset, 1, &handle);
}

void CommandContext::BindResources(int rootIndex, int offset, const D3D12_CPU_DESCRIPTOR_HANDLE* handles, int count)
{
	m_ShaderResourceDescriptorAllocator.SetDescriptors(rootIndex, offset, count, handles);
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
	return m_DynamicAllocator->Allocate(size, alignment);
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
		renderPassDepthStencilDesc.DepthBeginningAccess.Clear.ClearValue.Format = renderPassInfo.DepthStencilTarget.Target->GetFormat();
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
		renderPassDepthStencilDesc.StencilBeginningAccess.Clear.ClearValue.Format = renderPassInfo.DepthStencilTarget.Target->GetFormat();
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
			clearValue.Format = data.Target->GetFormat();
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
			renderTargetDescs[i].EndingAccess.Resolve.Format = data.Target->GetFormat();
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
			CopyTexture(data.Target, data.ResolveTarget);
		}
	}

	m_InRenderPass = false;
}

void CommandContext::Draw(int vertexStart, int vertexCount)
{
	check(m_pCurrentPSO && m_pCurrentPSO->GetType() == PipelineStateType::Graphics);
	check(m_CurrentCommandContext == CommandListContext::Graphics);
	PrepareDraw();
	m_pCommandList->DrawInstanced(vertexCount, 1, vertexStart, 0);
}

void CommandContext::DrawIndexed(int indexCount, int indexStart, int minVertex /*= 0*/)
{
	check(m_pCurrentPSO && m_pCurrentPSO->GetType() == PipelineStateType::Graphics);
	check(m_CurrentCommandContext == CommandListContext::Graphics);
	PrepareDraw();
	m_pCommandList->DrawIndexedInstanced(indexCount, 1, indexStart, minVertex, 0);
}

void CommandContext::DrawIndexedInstanced(int indexCount, int indexStart, int instanceCount, int minVertex /*= 0*/, int instanceStart /*= 0*/)
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

void CommandContext::ResolveResource(Texture* pSource, uint32 sourceSubResource, Texture* pTarget, uint32 targetSubResource, DXGI_FORMAT format)
{
	FlushResourceBarriers();
	m_pCommandList->ResolveSubresource(pTarget->GetResource(), targetSubResource, pSource->GetResource(), sourceSubResource, format);
}

void CommandContext::PrepareDraw()
{
	check(m_CurrentCommandContext != CommandListContext::Invalid);
	FlushResourceBarriers();
	m_ShaderResourceDescriptorAllocator.BindStagedDescriptors(m_pCommandList, m_CurrentCommandContext);
}

void CommandContext::SetPipelineState(PipelineState* pPipelineState)
{
	pPipelineState->ConditionallyReload();
	m_pCommandList->SetPipelineState(pPipelineState->GetPipelineState());
	m_pCurrentPSO = pPipelineState;
}

void CommandContext::SetPipelineState(StateObject* pStateObject)
{
	check(m_pRaytracingCommandList);
	pStateObject->ConditionallyReload();
	m_pRaytracingCommandList->SetPipelineState1(pStateObject->GetStateObject());
	m_pCurrentSO = pStateObject;
}

void CommandContext::SetDynamicVertexBuffer(int rootIndex, int elementCount, int elementSize, const void* pData)
{
	int bufferSize = elementCount * elementSize;
	DynamicAllocation allocation = m_DynamicAllocator->Allocate(bufferSize);
	memcpy(allocation.pMappedMemory, pData, bufferSize);
	D3D12_VERTEX_BUFFER_VIEW view = {};
	view.BufferLocation = allocation.GpuHandle;
	view.SizeInBytes = bufferSize;
	view.StrideInBytes = elementSize;
	m_pCommandList->IASetVertexBuffers(rootIndex, 1, &view);
}

void CommandContext::SetDynamicIndexBuffer(int elementCount, const void* pData, bool smallIndices /*= false*/)
{
	int stride = smallIndices ? sizeof(uint16) : sizeof(uint32);
	int bufferSize = elementCount * stride;
	DynamicAllocation allocation = m_DynamicAllocator->Allocate(bufferSize);
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

void CommandContext::SetVertexBuffers(const VertexBufferView* pVertexBuffers, int bufferCount)
{
	constexpr int MAX_VERTEX_BUFFERS = 4;
	checkf(bufferCount < MAX_VERTEX_BUFFERS, "VertexBuffer count (%d) exceeds the maximum (%d)", bufferCount, MAX_VERTEX_BUFFERS);
	std::array<D3D12_VERTEX_BUFFER_VIEW, MAX_VERTEX_BUFFERS> views = {};
	for (int i = 0; i < bufferCount; ++i)
	{
		views[i].BufferLocation = pVertexBuffers[i].Location;
		views[i].SizeInBytes = pVertexBuffers[i].Elements * pVertexBuffers[i].Stride;
		views[i].StrideInBytes = pVertexBuffers[i].Stride;
	}
	m_pCommandList->IASetVertexBuffers(0, bufferCount, views.data());
}

void CommandContext::SetIndexBuffer(const IndexBufferView& indexBuffer)
{
	D3D12_INDEX_BUFFER_VIEW view;
	view.BufferLocation = indexBuffer.Location;
	view.Format = indexBuffer.Format;
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

void ResourceBarrierBatcher::AddTransition(ID3D12Resource* pResource, D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState, int subResource)
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

CommandSignature::CommandSignature(GraphicsDevice* pParent)
	: GraphicsObject(pParent)
{

}

void CommandSignature::Finalize(const char* pName)
{
	D3D12_COMMAND_SIGNATURE_DESC desc{};
	desc.ByteStride = m_Stride;
	desc.NodeMask = 0;
	desc.NumArgumentDescs = (uint32)m_ArgumentDesc.size();
	desc.pArgumentDescs = m_ArgumentDesc.data();
	VERIFY_HR_EX(GetParent()->GetDevice()->CreateCommandSignature(&desc, m_pRootSignature, IID_PPV_ARGS(m_pCommandSignature.GetAddressOf())), GetParent()->GetDevice());
	D3D::SetObjectName(m_pCommandSignature.Get(), pName);
}

void CommandSignature::AddDispatch()
{
	D3D12_INDIRECT_ARGUMENT_DESC desc;
	desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
	m_ArgumentDesc.push_back(desc);
	m_Stride += sizeof(D3D12_DISPATCH_ARGUMENTS);
	m_IsCompute = true;
}

void CommandSignature::AddDispatchMesh()
{
	D3D12_INDIRECT_ARGUMENT_DESC desc;
	desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH;
	m_ArgumentDesc.push_back(desc);
	m_Stride += sizeof(D3D12_DISPATCH_MESH_ARGUMENTS);
	m_IsCompute = false;
}

void CommandSignature::AddDraw()
{
	D3D12_INDIRECT_ARGUMENT_DESC desc;
	desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
	m_ArgumentDesc.push_back(desc);
	m_Stride += sizeof(D3D12_DRAW_ARGUMENTS);
	m_IsCompute = false;
}

void CommandSignature::AddDrawIndexed()
{
	D3D12_INDIRECT_ARGUMENT_DESC desc;
	desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
	m_ArgumentDesc.push_back(desc);
	m_Stride += sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
	m_IsCompute = false;
}

void CommandSignature::AddConstants(uint32 numConstants, uint32 rootIndex, uint32 offset)
{
	D3D12_INDIRECT_ARGUMENT_DESC desc;
	desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
	desc.Constant.RootParameterIndex = rootIndex;
	desc.Constant.DestOffsetIn32BitValues = offset;
	desc.Constant.Num32BitValuesToSet = numConstants;
	m_ArgumentDesc.push_back(desc);
	m_Stride += numConstants * sizeof(uint32);
}

void CommandSignature::AddConstantBufferView(uint32 rootIndex)
{
	D3D12_INDIRECT_ARGUMENT_DESC desc;
	desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW;
	desc.ConstantBufferView.RootParameterIndex = rootIndex;
	m_ArgumentDesc.push_back(desc);
	m_Stride += sizeof(uint64);
}

void CommandSignature::AddShaderResourceView(uint32 rootIndex)
{
	D3D12_INDIRECT_ARGUMENT_DESC desc;
	desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW;
	desc.ShaderResourceView.RootParameterIndex = rootIndex;
	m_ArgumentDesc.push_back(desc);
	m_Stride += 8;
}

void CommandSignature::AddUnorderedAccessView(uint32 rootIndex)
{
	D3D12_INDIRECT_ARGUMENT_DESC desc;
	desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW;
	desc.UnorderedAccessView.RootParameterIndex = rootIndex;
	m_ArgumentDesc.push_back(desc);
	m_Stride += 8;
}

void CommandSignature::AddVertexBuffer(uint32 slot)
{
	D3D12_INDIRECT_ARGUMENT_DESC desc;
	desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW;
	desc.VertexBuffer.Slot = slot;
	m_ArgumentDesc.push_back(desc);
	m_Stride += sizeof(D3D12_VERTEX_BUFFER_VIEW);
}

void CommandSignature::AddIndexBuffer()
{
	D3D12_INDIRECT_ARGUMENT_DESC desc;
	desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW;
	m_ArgumentDesc.push_back(desc);
	m_Stride += sizeof(D3D12_INDEX_BUFFER_VIEW);
}
