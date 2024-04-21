#include "stdafx.h"
#include "CommandContext.h"
#include "Graphics.h"
#include "CommandQueue.h"
#include "GPUDescriptorHeap.h"
#include "PipelineState.h"
#include "RootSignature.h"
#include "Buffer.h"
#include "Texture.h"
#include "ResourceViews.h"
#include "ShaderBindingTable.h"
#include "StateObject.h"
#include "Core/Profiler.h"

CommandContext::CommandContext(GraphicsDevice* pParent, Ref<ID3D12CommandList> pCommandList, D3D12_COMMAND_LIST_TYPE type, GPUDescriptorHeap* pDescriptorHeap, ScratchAllocationManager* pScratchAllocationManager)
	: DeviceObject(pParent),
	m_ShaderResourceDescriptorAllocator(pDescriptorHeap),
	m_ScratchAllocator(pScratchAllocationManager),
	m_Type(type)
{
	check(pCommandList.As(&m_pCommandList));

	ID3D12Device* pDevice = pParent->GetDevice();
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	heapDesc.NumDescriptors = 1;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	VERIFY_HR(pDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(m_pDSVHeap.GetAddressOf())));

	heapDesc.NumDescriptors = D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	VERIFY_HR(pDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(m_pRTVHeap.GetAddressOf())));
	m_RTVSize = pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
}

void CommandContext::Reset()
{
	check(m_pCommandList);
	if (m_pAllocator == nullptr)
	{
		m_pAllocator = GetParent()->GetCommandQueue(m_Type)->RequestAllocator();
		m_pCommandList->Reset(m_pAllocator, nullptr);
	}

	check(m_NumBatchedBarriers == 0);
	check(m_PendingBarriers.empty());
	m_ResourceStates.clear();

	ClearState();
}

SyncPoint CommandContext::Execute()
{
	return Execute({ this });
}

SyncPoint CommandContext::Execute(Span<CommandContext* const> contexts)
{
	check(contexts.GetSize() > 0);
	CommandQueue* pQueue = contexts[0]->GetParent()->GetCommandQueue(contexts[0]->GetType());
	for(CommandContext* pContext : contexts)
	{
		check(pContext->GetType() == pQueue->GetType(), "All commandlist types must match. Expected %s, got %s",
			D3D::CommandlistTypeToString(pQueue->GetType()), D3D::CommandlistTypeToString(pContext->GetType()));
		pContext->FlushResourceBarriers();
	}
	SyncPoint syncPoint = pQueue->ExecuteCommandLists(contexts);
	for (CommandContext* pContext : contexts)
	{
		pContext->Free(syncPoint);
	}
	return syncPoint;
}

void CommandContext::Free(const SyncPoint& syncPoint)
{
	m_ScratchAllocator.Free(syncPoint);
	GetParent()->GetCommandQueue(m_Type)->FreeAllocator(syncPoint, m_pAllocator);
	m_pAllocator = nullptr;
	GetParent()->FreeCommandList(this);

	if (m_Type != D3D12_COMMAND_LIST_TYPE_COPY)
	{
		m_ShaderResourceDescriptorAllocator.ReleaseUsedHeaps(syncPoint);
	}
}

void CommandContext::ClearState()
{
	if (m_Type != D3D12_COMMAND_LIST_TYPE_COPY)
	{
		FlushResourceBarriers();

		m_CurrentCommandContext = CommandListContext::Invalid;

		m_pCurrentPSO = nullptr;
		m_pCurrentSO = nullptr;
		m_pCurrentRS = nullptr;

		m_pCommandList->ClearState(nullptr);

		ID3D12DescriptorHeap* pHeaps[] =
		{
			GetParent()->GetGlobalViewHeap()->GetHeap(),
			GetParent()->GetGlobalSamplerHeap()->GetHeap(),
		};
		m_pCommandList->SetDescriptorHeaps(ARRAYSIZE(pHeaps), pHeaps);
	}
}


void CommandContext::InsertResourceBarrier(DeviceResource* pResource, D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState, uint32 subResource /*= D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES*/)
{
	check(!m_InRenderPass);
	check(pResource && pResource->GetResource());
	check(pResource->UseStateTracking());
	check(D3D::IsTransitionAllowed(m_Type, beforeState), "Before state (%s) is not valid on this commandlist type (%s)", D3D::ResourceStateToString(beforeState).c_str(), D3D::CommandlistTypeToString(m_Type));
	check(D3D::IsTransitionAllowed(m_Type, afterState), "After state (%s) is not valid on this commandlist type (%s)", D3D::ResourceStateToString(afterState).c_str(), D3D::CommandlistTypeToString(m_Type));

	ResourceState& localResourceState = m_ResourceStates[pResource];
	D3D12_RESOURCE_STATES localBeforeState = localResourceState.Get(subResource);
	check(beforeState == D3D12_RESOURCE_STATE_UNKNOWN || localBeforeState == D3D12_RESOURCE_STATE_UNKNOWN || localBeforeState == beforeState, "Provided before state %s of resource %s does not match with tracked resource state %s",
		D3D::ResourceStateToString(beforeState), pResource->GetName(), D3D::ResourceStateToString(localBeforeState));

	// If the given before state is "Unknown", get it from the commandlist
	if(beforeState == D3D12_RESOURCE_STATE_UNKNOWN)
		beforeState = localBeforeState;

	if (beforeState == D3D12_RESOURCE_STATE_UNKNOWN)
	{
		localResourceState.Set(afterState, subResource);

		PendingBarrier& barrier = m_PendingBarriers.emplace_back();
		barrier.pResource = pResource;
		barrier.State = afterState;
		barrier.Subresource = subResource;
	}
	else
	{
		if (D3D::NeedsTransition(beforeState, afterState, true))
		{
			if (m_NumBatchedBarriers > 0)
			{
				// If the previous barrier is for the same resource, see if we can combine the barrier.
				D3D12_RESOURCE_BARRIER& last = m_BatchedBarriers[m_NumBatchedBarriers - 1];
				if (last.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION
					&& last.Transition.pResource == pResource->GetResource()
					&& last.Transition.StateBefore == beforeState
					&& D3D::CanCombineResourceState(afterState, last.Transition.StateAfter))
				{
					last.Transition.StateAfter |= afterState;
					return;
				}
			}
			AddBarrier(CD3DX12_RESOURCE_BARRIER::Transition(pResource->GetResource(),
						beforeState,
						afterState,
						subResource,
						D3D12_RESOURCE_BARRIER_FLAG_NONE)
			);

			localResourceState.Set(afterState, subResource);
		}
	}
}

void CommandContext::InsertAliasingBarrier(const DeviceResource* pResource)
{
	AddBarrier(CD3DX12_RESOURCE_BARRIER::Aliasing(nullptr, pResource->GetResource()));
}

void CommandContext::InsertUAVBarrier(const DeviceResource* pResource /*= nullptr*/)
{
	AddBarrier(CD3DX12_RESOURCE_BARRIER::UAV(pResource ? pResource->GetResource() : nullptr));
}

void CommandContext::FlushResourceBarriers()
{
	if (m_NumBatchedBarriers > 0)
	{
		m_pCommandList->ResourceBarrier(m_NumBatchedBarriers, m_BatchedBarriers.data());
		m_NumBatchedBarriers = 0;
	}
}

void CommandContext::CopyResource(const DeviceResource* pSource, const DeviceResource* pTarget)
{
	check(pSource && pSource->GetResource(), "Source is invalid");
	check(pTarget && pTarget->GetResource(), "Target is invalid");

	FlushResourceBarriers();
	m_pCommandList->CopyResource(pTarget->GetResource(), pSource->GetResource());
}

void CommandContext::CopyTexture(const Texture* pSource, const Buffer* pTarget, const D3D12_BOX& sourceRegion, uint32 sourceSubresource /*= 0*/, uint32 destinationOffset /*= 0*/)
{
	check(pSource && pSource->GetResource(), "Source is invalid");
	check(pTarget && pTarget->GetResource(), "Target is invalid");

	D3D12_PLACED_SUBRESOURCE_FOOTPRINT textureFootprint;
	textureFootprint.Offset = 0;
	textureFootprint.Footprint.Width = sourceRegion.right - sourceRegion.left;
	textureFootprint.Footprint.Depth = sourceRegion.back - sourceRegion.front;
	textureFootprint.Footprint.Height = sourceRegion.bottom - sourceRegion.top;
	textureFootprint.Footprint.Format = D3D::ConvertFormat(pSource->GetFormat());
	textureFootprint.Footprint.RowPitch = Math::AlignUp<uint32>((uint32)RHI::GetRowPitch(pSource->GetFormat(), textureFootprint.Footprint.Width), D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);

	CD3DX12_TEXTURE_COPY_LOCATION srcLocation(pSource->GetResource(), sourceSubresource);
	CD3DX12_TEXTURE_COPY_LOCATION dstLocation(pTarget->GetResource(), textureFootprint);
	FlushResourceBarriers();
	m_pCommandList->CopyTextureRegion(&dstLocation, destinationOffset, 0, 0, &srcLocation, &sourceRegion);
}

void CommandContext::CopyTexture(const Texture* pSource, const Texture* pTarget, const D3D12_BOX& sourceRegion, const D3D12_BOX& destinationRegion, uint32 sourceSubresource /*= 0*/, uint32 destinationSubregion /*= 0*/)
{
	check(pSource && pSource->GetResource(), "Source is invalid");
	check(pTarget && pTarget->GetResource(), "Target is invalid");

	CD3DX12_TEXTURE_COPY_LOCATION srcLocation(pSource->GetResource(), sourceSubresource);
	CD3DX12_TEXTURE_COPY_LOCATION dstLocation(pTarget->GetResource(), destinationSubregion);
	FlushResourceBarriers();
	m_pCommandList->CopyTextureRegion(&dstLocation, destinationRegion.left, destinationRegion.top, destinationRegion.front, &srcLocation, &sourceRegion);
}

void CommandContext::CopyBuffer(const Buffer* pSource, const Buffer* pTarget, uint64 size, uint64 sourceOffset, uint64 destinationOffset)
{
	check(pSource && pSource->GetResource(), "Source is invalid");
	check(pTarget && pTarget->GetResource(), "Target is invalid");

	FlushResourceBarriers();
	m_pCommandList->CopyBufferRegion(pTarget->GetResource(), destinationOffset, pSource->GetResource(), sourceOffset, size);
}

void CommandContext::Dispatch(uint32 groupCountX, uint32 groupCountY, uint32 groupCountZ)
{
	check(m_pCurrentPSO);
	check(m_CurrentCommandContext == CommandListContext::Compute);
	check(
		groupCountX <= D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION &&
		groupCountY <= D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION &&
		groupCountZ <= D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION,
		"Dispatch group size (%d x %d x %d) can not exceed %d", groupCountX, groupCountY, groupCountZ, D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION);
	
	PrepareDraw();
	if(groupCountX > 0 && groupCountY > 0 && groupCountZ > 0)
		m_pCommandList->Dispatch(groupCountX, groupCountY, groupCountZ);
}

void CommandContext::Dispatch(const Vector3i& groupCounts)
{
	Dispatch(groupCounts.x, groupCounts.y, groupCounts.z);
}

void CommandContext::DispatchMesh(uint32 groupCountX, uint32 groupCountY /*= 1*/, uint32 groupCountZ /*= 1*/)
{
	check(m_pCurrentPSO);
	check(m_CurrentCommandContext == CommandListContext::Graphics);
	
	PrepareDraw();
	m_pCommandList->DispatchMesh(groupCountX, groupCountY, groupCountZ);
}

void CommandContext::DispatchMesh(const Vector3i& groupCounts)
{
	DispatchMesh(groupCounts.x, groupCounts.y, groupCounts.z);
}

void CommandContext::ExecuteIndirect(const CommandSignature* pCommandSignature, uint32 maxCount, const Buffer* pIndirectArguments, const Buffer* pCountBuffer, uint32 argumentsOffset /*= 0*/, uint32 countOffset /*= 0*/)
{
	check(m_pCurrentPSO || m_pCurrentSO);
	
	PrepareDraw();
	m_pCommandList->ExecuteIndirect(pCommandSignature->GetCommandSignature(), maxCount, pIndirectArguments->GetResource(), argumentsOffset, pCountBuffer ? pCountBuffer->GetResource() : nullptr, countOffset);
}

void CommandContext::ClearUAVu(const UnorderedAccessView* pUAV, const Vector4u& values)
{
	check(pUAV);
	DescriptorHandle gpuHandle = pUAV->GetGPUDescriptor();
	if (gpuHandle.IsNull())
	{
		gpuHandle = m_ShaderResourceDescriptorAllocator.Allocate(1);
		GetParent()->GetDevice()->CopyDescriptorsSimple(1, gpuHandle.CpuHandle, pUAV->GetDescriptor(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}

	FlushResourceBarriers();
	m_pCommandList->ClearUnorderedAccessViewUint(gpuHandle.GpuHandle, pUAV->GetDescriptor(), pUAV->GetResource()->GetResource(), &values.x, 0, nullptr);
}

void CommandContext::ClearUAVf(const UnorderedAccessView* pUAV, const Vector4& values)
{
	check(pUAV);
	DescriptorHandle gpuHandle = pUAV->GetGPUDescriptor();
	if (gpuHandle.IsNull())
	{
		gpuHandle = m_ShaderResourceDescriptorAllocator.Allocate(1);
		GetParent()->GetDevice()->CopyDescriptorsSimple(1, gpuHandle.CpuHandle, pUAV->GetDescriptor(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}

	FlushResourceBarriers();
	m_pCommandList->ClearUnorderedAccessViewFloat(gpuHandle.GpuHandle, pUAV->GetDescriptor(), pUAV->GetResource()->GetResource(), &values.x, 0, nullptr);
}

void CommandContext::SetComputeRootSignature(const RootSignature* pRootSignature)
{
	m_pCommandList->SetComputeRootSignature(pRootSignature->GetRootSignature());
	m_ShaderResourceDescriptorAllocator.ParseRootSignature(pRootSignature);
	m_CurrentCommandContext = CommandListContext::Compute;
	m_pCurrentRS = pRootSignature;
}

void CommandContext::SetGraphicsRootSignature(const RootSignature* pRootSignature)
{
	m_pCommandList->SetGraphicsRootSignature(pRootSignature->GetRootSignature());
	m_ShaderResourceDescriptorAllocator.ParseRootSignature(pRootSignature);
	m_CurrentCommandContext = CommandListContext::Graphics;
	m_pCurrentRS = pRootSignature;
}

void CommandContext::BindRootSRV(uint32 rootIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
{
	check(m_CurrentCommandContext != CommandListContext::Invalid);
	FlushResourceBarriers();
	if (m_CurrentCommandContext == CommandListContext::Graphics)
		m_pCommandList->SetGraphicsRootShaderResourceView(rootIndex, address);
	else
		m_pCommandList->SetComputeRootShaderResourceView(rootIndex, address);
}

void CommandContext::BindRootUAV(uint32 rootIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
{
	check(m_CurrentCommandContext != CommandListContext::Invalid);
	FlushResourceBarriers();
	if (m_CurrentCommandContext == CommandListContext::Graphics)
		m_pCommandList->SetGraphicsRootUnorderedAccessView(rootIndex, address);
	else
		m_pCommandList->SetComputeRootUnorderedAccessView(rootIndex, address);
}

void CommandContext::BindRootCBV(uint32 rootIndex, const void* pData, uint32 dataSize)
{
	check(m_CurrentCommandContext != CommandListContext::Invalid);

	bool isRootConstants = m_pCurrentRS->IsRootConstant(rootIndex);
	if (isRootConstants)
	{
		check(dataSize % sizeof(uint32) == 0);
		uint32 rootConstantsSize = m_pCurrentRS->GetNumRootConstants(rootIndex) * sizeof(uint32);
		check(dataSize <= rootConstantsSize);

#ifdef _DEBUG
		// In debug, write 0xCDCDCDCD to unwritten root constants
		if (rootConstantsSize != dataSize)
		{
			void* pLocalData = _alloca(rootConstantsSize);
			memset(pLocalData, (int)0xCDCDCDCD, rootConstantsSize);
			memcpy(pLocalData, pData, dataSize);
			dataSize = rootConstantsSize;
			pData = pLocalData;
		}
#endif

		if (m_CurrentCommandContext == CommandListContext::Graphics)
			m_pCommandList->SetGraphicsRoot32BitConstants(rootIndex, dataSize / sizeof(uint32), pData, 0);
		else
			m_pCommandList->SetComputeRoot32BitConstants(rootIndex, dataSize / sizeof(uint32), pData, 0);
	}
	else
	{
		ScratchAllocation allocation = AllocateScratch(dataSize, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
		memcpy(allocation.pMappedMemory, pData, dataSize);

		if (m_CurrentCommandContext == CommandListContext::Graphics)
			m_pCommandList->SetGraphicsRootConstantBufferView(rootIndex, allocation.GpuHandle);
		else
			m_pCommandList->SetComputeRootConstantBufferView(rootIndex, allocation.GpuHandle);
	}
}

void CommandContext::BindResources(uint32 rootIndex, Span<const ResourceView*> pViews, uint32 offset)
{
	m_ShaderResourceDescriptorAllocator.SetDescriptors(rootIndex, offset, pViews);
}

void CommandContext::SetShadingRate(D3D12_SHADING_RATE shadingRate /*= D3D12_SHADING_RATE_1X1*/)
{
	m_pCommandList->RSSetShadingRate(shadingRate, nullptr);
}

void CommandContext::SetShadingRateImage(Texture* pTexture)
{
	m_pCommandList->RSSetShadingRateImage(pTexture->GetResource());
}

ScratchAllocation CommandContext::AllocateScratch(uint64 size, uint32 alignment /*= 16*/)
{
	return m_ScratchAllocator.Allocate(size, alignment);
}

void CommandContext::ResolvePendingBarriers(CommandContext& resolveContext)
{
	if (m_PendingBarriers.empty())
		return;

	PROFILE_GPU_SCOPE(resolveContext.GetCommandList());
	PROFILE_CPU_SCOPE();

	for (const PendingBarrier& pending : m_PendingBarriers)
	{
		uint32 subResource = pending.Subresource;
		DeviceResource* pResource = pending.pResource;

		// Retrieve the last known resource state
		D3D12_RESOURCE_STATES beforeState = pResource->GetResourceState(subResource);
		check(D3D::IsTransitionAllowed(m_Type, beforeState),
			"Resource (%s) can not be transitioned from this state (%s) on this queue (%s). Insert a barrier on another queue before executing this one.",
			pResource->GetName(), D3D::ResourceStateToString(beforeState).c_str(), D3D::CommandlistTypeToString(m_Type));

		// Get the after state of the first use in the current cmdlist
		D3D12_RESOURCE_STATES afterState = pending.State;
		if(D3D::NeedsTransition(beforeState, afterState, false))
			resolveContext.AddBarrier(CD3DX12_RESOURCE_BARRIER::Transition(pResource->GetResource(), beforeState, afterState, subResource));

		// Update the resource with the last known state of the current cmdlist
		D3D12_RESOURCE_STATES end_state = GetLocalResourceState(pending.pResource, subResource);
		pResource->SetResourceState(end_state, subResource);
	}
	resolveContext.FlushResourceBarriers();
	m_PendingBarriers.clear();
}

void CommandContext::BeginRenderPass(const RenderPassInfo& renderPassInfo)
{
	check(!m_InRenderPass, "Already in RenderPass");

	FlushResourceBarriers();

	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = {};

	D3D12_CLEAR_FLAGS clearFlags = (D3D12_CLEAR_FLAGS)0;
	if (EnumHasAllFlags(renderPassInfo.DepthStencilTarget.Flags, RenderPassDepthFlags::ClearDepth))
		clearFlags |= D3D12_CLEAR_FLAGS::D3D12_CLEAR_FLAG_DEPTH;

	if (EnumHasAllFlags(renderPassInfo.DepthStencilTarget.Flags, RenderPassDepthFlags::ClearStencil))
		clearFlags |= D3D12_CLEAR_FLAGS::D3D12_CLEAR_FLAG_STENCIL;

	if (renderPassInfo.DepthStencilTarget.Target)
	{
		dsvHandle = m_pDSVHeap->GetCPUDescriptorHandleForHeapStart();

		D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
		const TextureDesc& desc = renderPassInfo.DepthStencilTarget.Target->GetDesc();
		dsvDesc.Format = D3D::ConvertFormat(desc.Format);
		switch (desc.Type)
		{
		case TextureType::Texture1D:
			dsvDesc.Texture1D.MipSlice = 0;
			dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1D;
			break;
		case TextureType::Texture1DArray:
			dsvDesc.Texture1DArray.ArraySize = desc.DepthOrArraySize;
			dsvDesc.Texture1DArray.FirstArraySlice = 0;
			dsvDesc.Texture1DArray.MipSlice = 0;
			dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1DARRAY;
			break;
		case TextureType::Texture2D:
			dsvDesc.Texture2D.MipSlice = 0;
			dsvDesc.ViewDimension = desc.SampleCount > 1 ? D3D12_DSV_DIMENSION_TEXTURE2DMS : D3D12_DSV_DIMENSION_TEXTURE2D;
			break;
		case TextureType::Texture3D:
		case TextureType::Texture2DArray:
			dsvDesc.Texture2DArray.ArraySize = desc.DepthOrArraySize;
			dsvDesc.Texture2DArray.FirstArraySlice = 0;
			dsvDesc.Texture2DArray.MipSlice = 0;
			dsvDesc.ViewDimension = desc.SampleCount > 1 ? D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY : D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
			break;
		case TextureType::TextureCube:
		case TextureType::TextureCubeArray:
			dsvDesc.Texture2DArray.ArraySize = desc.DepthOrArraySize * 6;
			dsvDesc.Texture2DArray.FirstArraySlice = 0;
			dsvDesc.Texture2DArray.MipSlice = 0;
			dsvDesc.ViewDimension = desc.SampleCount > 1 ? D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY : D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
			break;
		default:
			break;
		}
		if(EnumHasAllFlags(renderPassInfo.DepthStencilTarget.Flags, RenderPassDepthFlags::ReadOnlyDepth))
			dsvDesc.Flags |= D3D12_DSV_FLAG_READ_ONLY_DEPTH;
		if (EnumHasAllFlags(renderPassInfo.DepthStencilTarget.Flags, RenderPassDepthFlags::ReadOnlyStencil))
			dsvDesc.Flags |= D3D12_DSV_FLAG_READ_ONLY_STENCIL;
		GetParent()->GetDevice()->CreateDepthStencilView(renderPassInfo.DepthStencilTarget.Target->GetResource(), &dsvDesc, dsvHandle);
	}

	if (clearFlags != (D3D12_CLEAR_FLAGS)0)
	{
		const ClearBinding& clearBinding = renderPassInfo.DepthStencilTarget.Target->GetClearBinding();
		check(clearBinding.BindingValue == ClearBinding::ClearBindingValue::DepthStencil);
		m_pCommandList->ClearDepthStencilView(dsvHandle, clearFlags, clearBinding.DepthStencil.Depth, clearBinding.DepthStencil.Stencil, 0, nullptr);
	}

	std::array<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT> rtvs;
	for (uint32 i = 0; i < renderPassInfo.RenderTargetCount; ++i)
	{
		const RenderPassInfo::RenderTargetInfo& data = renderPassInfo.RenderTargets[i];

		D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		const TextureDesc& desc = data.Target->GetDesc();
		rtvDesc.Format = D3D::ConvertFormat(desc.Format);
		switch (desc.Type)
		{
		case TextureType::Texture1D:
			rtvDesc.Texture1D.MipSlice = 0;
			rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1D;
			break;
		case TextureType::Texture1DArray:
			rtvDesc.Texture1DArray.ArraySize = desc.DepthOrArraySize;
			rtvDesc.Texture1DArray.FirstArraySlice = 0;
			rtvDesc.Texture1DArray.MipSlice = 0;
			rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1DARRAY;
			break;
		case TextureType::Texture2D:
			rtvDesc.Texture2D.MipSlice = 0;
			rtvDesc.Texture2D.PlaneSlice = 0;
			rtvDesc.ViewDimension = desc.SampleCount > 1 ? D3D12_RTV_DIMENSION_TEXTURE2DMS : D3D12_RTV_DIMENSION_TEXTURE2D;
			break;
		case TextureType::TextureCube:
		case TextureType::TextureCubeArray:
		case TextureType::Texture2DArray:
			rtvDesc.Texture2DArray.MipSlice = 0;
			rtvDesc.Texture2DArray.PlaneSlice = 0;
			rtvDesc.Texture2DArray.ArraySize = desc.DepthOrArraySize;
			rtvDesc.Texture2DArray.FirstArraySlice = 0;
			rtvDesc.ViewDimension = desc.SampleCount > 1 ? D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY : D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
			break;
		case TextureType::Texture3D:
			rtvDesc.Texture3D.FirstWSlice = 0;
			rtvDesc.Texture3D.MipSlice = 0;
			rtvDesc.Texture3D.WSize = desc.DepthOrArraySize;
			rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
			break;
		default:
			break;
		}

		D3D12_CPU_DESCRIPTOR_HANDLE rtv = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_pRTVHeap->GetCPUDescriptorHandleForHeapStart(), i, m_RTVSize);
		GetParent()->GetDevice()->CreateRenderTargetView(data.Target->GetResource(), &rtvDesc, rtv);

		if (EnumHasAllFlags(data.Flags, RenderPassColorFlags::Clear))
		{
			check(data.Target->GetClearBinding().BindingValue == ClearBinding::ClearBindingValue::Color);
			m_pCommandList->ClearRenderTargetView(rtv, &data.Target->GetClearBinding().Color.x, 0, nullptr);
		}
		rtvs[i] = rtv;
	}
	m_pCommandList->OMSetRenderTargets(renderPassInfo.RenderTargetCount, rtvs.data(), false, dsvHandle.ptr != 0 ? &dsvHandle : nullptr);

	m_InRenderPass = true;
	m_CurrentRenderPassInfo = renderPassInfo;

	Texture* pTargetTexture = renderPassInfo.DepthStencilTarget.Target ? renderPassInfo.DepthStencilTarget.Target : renderPassInfo.RenderTargets[0].Target;
	SetViewport(FloatRect(0, 0, (float)pTargetTexture->GetWidth(), (float)pTargetTexture->GetHeight()), 0, 1);
}

void CommandContext::EndRenderPass()
{
	check(m_InRenderPass);

	for (uint32 i = 0; i < m_CurrentRenderPassInfo.RenderTargetCount; ++i)
	{
		const RenderPassInfo::RenderTargetInfo& data = m_CurrentRenderPassInfo.RenderTargets[i];
		if (EnumHasAllFlags(data.Flags, RenderPassColorFlags::Resolve))
		{
			if (data.Target->GetDesc().SampleCount > 1)
			{
				InsertResourceBarrier(data.Target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
				InsertResourceBarrier(data.ResolveTarget, D3D12_RESOURCE_STATE_UNKNOWN, D3D12_RESOURCE_STATE_RESOLVE_DEST);
				uint32 subResource = D3D12CalcSubresource(data.MipLevel, data.ArrayIndex, 0, data.Target->GetMipLevels(), data.Target->GetArraySize());
				ResolveResource(data.Target, subResource, data.ResolveTarget, 0, data.Target->GetFormat());
			}
			else if (data.Target != data.ResolveTarget)
			{
				E_LOG(Warning, "RenderTarget %u is set to resolve but has a sample count of 1. This will just do a CopyTexture instead which is wasteful.", i);
				CopyResource(data.Target, data.ResolveTarget);
			}
		}
	}

	m_InRenderPass = false;
}

void CommandContext::Draw(uint32 vertexStart, uint32 vertexCount, uint32 instances, uint32 instanceStart)
{
	check(m_pCurrentPSO);
	check(m_CurrentCommandContext == CommandListContext::Graphics);
	PrepareDraw();
	m_pCommandList->DrawInstanced(vertexCount, instances, vertexStart, instanceStart);
}

void CommandContext::DrawIndexedInstanced(uint32 indexCount, uint32 indexStart, uint32 instanceCount, uint32 minVertex /*= 0*/, uint32 instanceStart /*= 0*/)
{
	check(m_pCurrentPSO);
	check(m_CurrentCommandContext == CommandListContext::Graphics);
	PrepareDraw();
	m_pCommandList->DrawIndexedInstanced(indexCount, instanceCount, indexStart, minVertex, instanceStart);
}

void CommandContext::DispatchRays(ShaderBindingTable& table, uint32 width /*= 1*/, uint32 height /*= 1*/, uint32 depth /*= 1*/)
{
	check(m_pCurrentSO);
	check(m_CurrentCommandContext == CommandListContext::Compute);
	D3D12_DISPATCH_RAYS_DESC desc{};
	table.Commit(*this, desc);
	desc.Width = width;
	desc.Height = height;
	desc.Depth = depth;
	PrepareDraw();
	m_pCommandList->DispatchRays(&desc);
}

void CommandContext::DispatchGraph(const D3D12_DISPATCH_GRAPH_DESC& graphDesc)
{
	check(m_CurrentCommandContext == CommandListContext::Compute);
	PrepareDraw();
	m_pCommandList->DispatchGraph(&graphDesc);
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
	if (m_pCurrentSO != pStateObject)
	{
		pStateObject->ConditionallyReload();
		m_pCommandList->SetPipelineState1(pStateObject->GetStateObject());
		m_pCurrentSO = pStateObject;
	}
}

void CommandContext::SetProgram(const D3D12_SET_PROGRAM_DESC& programDesc)
{
	m_pCommandList->SetProgram(&programDesc);
}

void CommandContext::SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY type)
{
	m_pCommandList->IASetPrimitiveTopology(type);
}

void CommandContext::SetVertexBuffers(Span<VertexBufferView> buffers)
{
	constexpr uint32 MAX_VERTEX_BUFFERS = 4;
	check(buffers.GetSize() < MAX_VERTEX_BUFFERS, "VertexBuffer count (%d) exceeds the maximum (%d)", buffers.GetSize(), MAX_VERTEX_BUFFERS);
	D3D12_VERTEX_BUFFER_VIEW views[MAX_VERTEX_BUFFERS];

	uint32 numViews = 0;
	for (const VertexBufferView& view : buffers)
	{
		views[numViews].BufferLocation = view.Location;
		views[numViews].SizeInBytes = view.Elements * view.Stride;
		views[numViews].StrideInBytes = view.Stride;
		++numViews;
	}
	m_pCommandList->IASetVertexBuffers(0, buffers.GetSize(), views);
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


void CommandContext::BindDynamicVertexBuffer(uint32 rootIndex, uint32 elementCount, uint32 elementSize, const void* pData)
{
	uint32 bufferSize = elementCount * elementSize;
	ScratchAllocation allocation = AllocateScratch(bufferSize);
	memcpy(allocation.pMappedMemory, pData, bufferSize);
	D3D12_VERTEX_BUFFER_VIEW view = {};
	view.BufferLocation = allocation.GpuHandle;
	view.SizeInBytes = bufferSize;
	view.StrideInBytes = elementSize;
	m_pCommandList->IASetVertexBuffers(rootIndex, 1, &view);
}

void CommandContext::BindDynamicIndexBuffer(uint32 elementCount, const void* pData, ResourceFormat format)
{
	uint32 bufferSize = (uint32)RHI::GetRowPitch(format, elementCount);
	ScratchAllocation allocation = AllocateScratch(bufferSize);
	memcpy(allocation.pMappedMemory, pData, bufferSize);
	D3D12_INDEX_BUFFER_VIEW view = {};
	view.BufferLocation = allocation.GpuHandle;
	view.SizeInBytes = bufferSize;
	view.Format = D3D::ConvertFormat(format);
	m_pCommandList->IASetIndexBuffer(&view);
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

void CommandContext::SetStencilRef(uint32 stencilRef)
{
	m_pCommandList->OMSetStencilRef(stencilRef);
}

void CommandContext::AddBarrier(const D3D12_RESOURCE_BARRIER& barrier)
{
	m_BatchedBarriers[m_NumBatchedBarriers++] = barrier;
	if(m_NumBatchedBarriers >= MaxNumBatchedBarriers)
		FlushResourceBarriers();
}

CommandSignature::CommandSignature(GraphicsDevice* pParent, ID3D12CommandSignature* pCmdSignature)
	: DeviceObject(pParent), m_pCommandSignature(pCmdSignature)
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
