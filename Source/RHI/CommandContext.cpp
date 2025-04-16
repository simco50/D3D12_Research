#include "stdafx.h"
#include "CommandContext.h"
#include "Device.h"
#include "CommandQueue.h"
#include "GPUDescriptorHeap.h"
#include "PipelineState.h"
#include "RootSignature.h"
#include "Buffer.h"
#include "Texture.h"
#include "ShaderBindingTable.h"
#include "StateObject.h"
#include "Core/Profiler.h"

CommandContext::CommandContext(GraphicsDevice* pParent, Ref<ID3D12CommandList> pCommandList, D3D12_COMMAND_LIST_TYPE type, ScratchAllocationManager* pScratchAllocationManager)
	: DeviceObject(pParent),
	m_Type(type)
{
	m_ScratchAllocator.Init(pScratchAllocationManager);

	gVerify(pCommandList.As(&m_pCommandList), == true);

	// Create DSV and RTV description heap per commandlist to create on-the-fly descriptors
	ID3D12Device*			   pDevice = pParent->GetDevice();
	{
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc{
			.Type			= D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
			.NumDescriptors = 1,
			.Flags			= D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
		};
		VERIFY_HR(pDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(m_pDSVHeap.GetAddressOf())));
		D3D::SetObjectName(m_pRTVHeap, "DSV Heap");
	}

	{
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc{
			.Type			= D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
			.NumDescriptors = D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT,
			.Flags			= D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
		};
		VERIFY_HR(pDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(m_pRTVHeap.GetAddressOf())));
		D3D::SetObjectName(m_pRTVHeap, "RTV Heap");
	}

	m_RTVSize = pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
}

void CommandContext::Reset()
{
	gAssert(m_pCommandList);
	if (m_pAllocator == nullptr)
	{
		m_pAllocator = GetParent()->AllocateCommandAllocator(m_Type);
		m_pCommandList->Reset(m_pAllocator, nullptr);
	}

	gAssert(m_BatchedBarriers.empty());
	gAssert(m_PendingBarriers.empty());
	m_ResourceStates.clear();

	ClearState();
}

void CommandContext::Free(const SyncPoint& syncPoint)
{
	m_ScratchAllocator.Free(syncPoint);
	GetParent()->FreeCommandAllocator(m_pAllocator, m_Type, syncPoint);
	m_pAllocator = nullptr;
	GetParent()->FreeCommandList(this);
}

void CommandContext::ClearState()
{
	if (m_Type != D3D12_COMMAND_LIST_TYPE_COPY)
	{
		FlushResourceBarriers();

		m_CurrentCommandContext = CommandListContext::Invalid;

		m_pCurrentPSO = nullptr;
		m_pCurrentSO = nullptr;
		m_pCurrentGraphicsRS = nullptr;
		m_pCurrentComputeRS = nullptr;

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
	gAssert(!m_InRenderPass);
	gAssert(pResource && pResource->GetResource());
	gAssert(beforeState != D3D12_RESOURCE_STATE_UNKNOWN || pResource->UseStateTracking());
	gAssert(D3D::IsTransitionAllowed(m_Type, beforeState), "Before state (%s) is not valid on this commandlist type (%s)", D3D::ResourceStateToString(beforeState).c_str(), D3D::CommandlistTypeToString(m_Type));
	gAssert(D3D::IsTransitionAllowed(m_Type, afterState), "After state (%s) is not valid on this commandlist type (%s)", D3D::ResourceStateToString(afterState).c_str(), D3D::CommandlistTypeToString(m_Type));

	if (beforeState == afterState)
		return;

	ResourceState& localResourceState = m_ResourceStates[pResource];
	D3D12_RESOURCE_STATES localBeforeState = localResourceState.Get(subResource);
	gAssert(beforeState == D3D12_RESOURCE_STATE_UNKNOWN || localBeforeState == D3D12_RESOURCE_STATE_UNKNOWN || localBeforeState == beforeState, "Provided before state %s of resource %s does not match with tracked resource state %s",
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
			if (!m_BatchedBarriers.empty())
			{
				// If the previous barrier is for the same resource, see if we can combine the barrier.
				D3D12_RESOURCE_BARRIER& last = m_BatchedBarriers.back();
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
	if (!m_BatchedBarriers.empty())
	{
		m_pCommandList->ResourceBarrier((UINT)m_BatchedBarriers.size(), m_BatchedBarriers.data());
		m_BatchedBarriers.clear();
	}
}

void CommandContext::CopyResource(const DeviceResource* pSource, const DeviceResource* pTarget)
{
	gAssert(pSource && pSource->GetResource(), "Source is invalid");
	gAssert(pTarget && pTarget->GetResource(), "Target is invalid");

	FlushResourceBarriers();
	m_pCommandList->CopyResource(pTarget->GetResource(), pSource->GetResource());
}

void CommandContext::CopyTexture(const Texture* pSource, const Buffer* pDestination, const Vector3u& sourceOrigin, const Vector3u sourceSize, uint32 sourceMip, uint32 sourceArrayIndex, uint32 destinationOffset)
{
	gAssert(pSource && pSource->GetResource(), "Source is invalid");
	gAssert(pDestination && pDestination->GetResource(), "Target is invalid");

	D3D12_PLACED_SUBRESOURCE_FOOTPRINT textureFootprint = {
		.Offset	   = 0,
		.Footprint = {
			.Format	  = D3D::ConvertFormat(pSource->GetFormat()),
			.Width	  = sourceSize.x,
			.Height	  = sourceSize.y,
			.Depth	  = sourceSize.z,
			.RowPitch = Math::AlignUp<uint32>((uint32)RHI::GetRowPitch(pSource->GetFormat(), sourceSize.x), D3D12_TEXTURE_DATA_PITCH_ALIGNMENT),
		}
	};

	uint32						  subresource = D3D12CalcSubresource(sourceMip, sourceArrayIndex, 0, pSource->GetMipLevels(), pSource->GetArraySize());
	CD3DX12_TEXTURE_COPY_LOCATION srcLocation(pSource->GetResource(), subresource);
	CD3DX12_TEXTURE_COPY_LOCATION dstLocation(pDestination->GetResource(), textureFootprint);
	FlushResourceBarriers();
	CD3DX12_BOX sourceRegion(sourceOrigin.x, sourceOrigin.y, sourceOrigin.z, sourceOrigin.x + sourceSize.x, sourceOrigin.y + sourceSize.y, sourceOrigin.z + sourceSize.z);
	m_pCommandList->CopyTextureRegion(&dstLocation, destinationOffset, 0, 0, &srcLocation, &sourceRegion);
}

void CommandContext::CopyTexture(const Texture* pSource, const Texture* pDestination, const Vector3u& sourceOrigin, const Vector3u sourceSize, const Vector3u& destinationOrigin, uint32 sourceMip, uint32 sourceArrayIndex, uint32 destinationMip, uint32 destinationArrayIndex)
{
	gAssert(pSource && pSource->GetResource(), "Source is invalid");
	gAssert(pDestination && pDestination->GetResource(), "Target is invalid");

	uint32						  sourceSubresource		 = D3D12CalcSubresource(sourceMip, sourceArrayIndex, 0, pSource->GetMipLevels(), pSource->GetArraySize());
	uint32						  destinationSubresource = D3D12CalcSubresource(destinationMip, destinationArrayIndex, 0, pDestination->GetMipLevels(), pDestination->GetArraySize());
	CD3DX12_TEXTURE_COPY_LOCATION srcLocation(pSource->GetResource(), sourceSubresource);
	CD3DX12_TEXTURE_COPY_LOCATION dstLocation(pDestination->GetResource(), destinationSubresource);
	FlushResourceBarriers();
	CD3DX12_BOX sourceRegion(sourceOrigin.x, sourceOrigin.y, sourceOrigin.z, sourceOrigin.x + sourceSize.x, sourceOrigin.y + sourceSize.y, sourceOrigin.z + sourceSize.z);
	m_pCommandList->CopyTextureRegion(&dstLocation, sourceOrigin.x, sourceOrigin.y, sourceOrigin.z, &srcLocation, &sourceRegion);
}

void CommandContext::CopyBuffer(const Buffer* pSource, const Buffer* pDestination, uint64 size, uint64 sourceOffset, uint64 destinationOffset)
{
	gAssert(pSource && pSource->GetResource(), "Source is invalid");
	gAssert(pDestination && pDestination->GetResource(), "Target is invalid");

	FlushResourceBarriers();
	m_pCommandList->CopyBufferRegion(pDestination->GetResource(), destinationOffset, pSource->GetResource(), sourceOffset, size);
}

void CommandContext::Dispatch(uint32 groupCountX, uint32 groupCountY, uint32 groupCountZ)
{
	gAssert(m_pCurrentPSO);
	gAssert(m_CurrentCommandContext == CommandListContext::Compute);
	gAssert(
		groupCountX <= D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION &&
			groupCountY <= D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION &&
			groupCountZ <= D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION,
		"Dispatch group size (%d x %d x %d) can not exceed %d", groupCountX, groupCountY, groupCountZ, D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION);

	PrepareDraw();
	if (groupCountX > 0 && groupCountY > 0 && groupCountZ > 0)
		m_pCommandList->Dispatch(groupCountX, groupCountY, groupCountZ);
}

void CommandContext::Dispatch(const Vector3i& groupCounts)
{
	Dispatch(groupCounts.x, groupCounts.y, groupCounts.z);
}

void CommandContext::DispatchMesh(uint32 groupCountX, uint32 groupCountY /*= 1*/, uint32 groupCountZ /*= 1*/)
{
	gAssert(m_pCurrentPSO);
	gAssert(m_CurrentCommandContext == CommandListContext::Graphics);

	PrepareDraw();
	m_pCommandList->DispatchMesh(groupCountX, groupCountY, groupCountZ);
}

void CommandContext::DispatchMesh(const Vector3i& groupCounts)
{
	DispatchMesh(groupCounts.x, groupCounts.y, groupCounts.z);
}

void CommandContext::ExecuteIndirect(const CommandSignature* pCommandSignature, uint32 maxCount, const Buffer* pIndirectArguments, const Buffer* pCountBuffer, uint32 argumentsOffset /*= 0*/, uint32 countOffset /*= 0*/)
{
	gAssert(m_pCurrentPSO || m_pCurrentSO);

	PrepareDraw();
	m_pCommandList->ExecuteIndirect(pCommandSignature->GetCommandSignature(), maxCount, pIndirectArguments->GetResource(), argumentsOffset, pCountBuffer ? pCountBuffer->GetResource() : nullptr, countOffset);
}

void CommandContext::ClearBufferFloat(const Buffer* pBuffer, float value)
{
	gAssert(pBuffer);
	DescriptorHandle gpuHandle = pBuffer->GetUAV();
	DescriptorHandle dynamicGPUHandle;
	if (!gpuHandle.IsValid() || pBuffer->GetDesc().IsStructured())
	{
		dynamicGPUHandle = GetParent()->CreateUAV(pBuffer, BufferUAVDesc(ResourceFormat::Unknown, true));
		gpuHandle		 = dynamicGPUHandle;
	}
	gAssert(gpuHandle.IsValid());

	FlushResourceBarriers();

	float values[4] = { value, value, value, value };
	DescriptorPtr ptr = GetParent()->FindResourceDescriptorPtr(gpuHandle);
	m_pCommandList->ClearUnorderedAccessViewFloat(ptr.GPUHandle, ptr.CPUOpaqueHandle, pBuffer->GetResource(), values, 0, nullptr);

	if (dynamicGPUHandle.IsValid())
		GetParent()->ReleaseResourceDescriptor(dynamicGPUHandle);

}

void CommandContext::ClearBufferUInt(const Buffer* pBuffer, uint32 value)
{
	gAssert(pBuffer);
	DescriptorHandle gpuHandle = pBuffer->GetUAV();
	DescriptorHandle dynamicGPUHandle;
	if (!gpuHandle.IsValid() || pBuffer->GetDesc().IsStructured())
	{
		dynamicGPUHandle = GetParent()->CreateUAV(pBuffer, BufferUAVDesc(ResourceFormat::Unknown, true));
		gpuHandle		 = dynamicGPUHandle;
	}
	gAssert(gpuHandle.IsValid());

	FlushResourceBarriers();

	uint32 values[4] = { value, value, value, value };
	DescriptorPtr ptr = GetParent()->FindResourceDescriptorPtr(gpuHandle);
	m_pCommandList->ClearUnorderedAccessViewUint(ptr.GPUHandle, ptr.CPUOpaqueHandle, pBuffer->GetResource(), values, 0, nullptr);

	if (dynamicGPUHandle.IsValid())
		GetParent()->ReleaseResourceDescriptor(dynamicGPUHandle);
}

void CommandContext::ClearTextureUInt(const Texture* pTexture, const Vector4u& values)
{
	gAssert(pTexture);
	DescriptorHandle gpuHandle = pTexture->GetUAV();
	gAssert(gpuHandle.IsValid());
	DescriptorPtr ptr = GetParent()->FindResourceDescriptorPtr(gpuHandle);

	FlushResourceBarriers();

	m_pCommandList->ClearUnorderedAccessViewUint(ptr.GPUHandle, ptr.CPUOpaqueHandle, pTexture->GetResource(), &values.x, 0, nullptr);
}

void CommandContext::ClearRenderTarget(const Texture* pTexture, const Vector4& values, uint32 mipLevel, uint32 arrayIndex)
{
	FlushResourceBarriers();
	D3D12_CPU_DESCRIPTOR_HANDLE rtv = GetRTV(0, pTexture, mipLevel, arrayIndex);
	m_pCommandList->ClearRenderTargetView(rtv, &values.x, 0, nullptr);
}

void CommandContext::ClearDepthStencil(const Texture* pTexture, RenderPassDepthFlags flags, float depth, uint8 stencil, uint32 mipLevel, uint32 arrayIndex)
{
	FlushResourceBarriers();
	D3D12_CPU_DESCRIPTOR_HANDLE dsv = GetDSV(pTexture, flags, mipLevel, arrayIndex);

	D3D12_CLEAR_FLAGS clearFlags = (D3D12_CLEAR_FLAGS)0;
	if (EnumHasAllFlags(flags, RenderPassDepthFlags::ClearDepth))
		clearFlags |= D3D12_CLEAR_FLAGS::D3D12_CLEAR_FLAG_DEPTH;
	if (EnumHasAllFlags(flags, RenderPassDepthFlags::ClearStencil))
		clearFlags |= D3D12_CLEAR_FLAGS::D3D12_CLEAR_FLAG_STENCIL;

	m_pCommandList->ClearDepthStencilView(dsv, clearFlags, depth, stencil, 0, nullptr);
}

void CommandContext::ClearTextureFloat(const Texture* pTexture, const Vector4& values)
{
	gAssert(pTexture);
	DescriptorHandle gpuHandle = pTexture->GetUAV();
	gAssert(gpuHandle.IsValid());
	DescriptorPtr ptr = GetParent()->FindResourceDescriptorPtr(gpuHandle);

	FlushResourceBarriers();

	m_pCommandList->ClearUnorderedAccessViewFloat(ptr.GPUHandle, ptr.CPUOpaqueHandle, pTexture->GetResource(), &values.x, 0, nullptr);
}

void CommandContext::SetComputeRootSignature(const RootSignature* pRootSignature)
{
	m_CurrentCommandContext = CommandListContext::Compute;
	if (pRootSignature != m_pCurrentComputeRS)
	{
		m_pCommandList->SetComputeRootSignature(pRootSignature->GetRootSignature());
		m_pCurrentComputeRS = pRootSignature;
	}
}

void CommandContext::SetGraphicsRootSignature(const RootSignature* pRootSignature)
{
	m_CurrentCommandContext = CommandListContext::Graphics;
	if (pRootSignature != m_pCurrentGraphicsRS)
	{
		m_pCommandList->SetGraphicsRootSignature(pRootSignature->GetRootSignature());
		m_pCurrentGraphicsRS = pRootSignature;
	}
}

void CommandContext::BindRootSRV(uint32 rootIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
{
	gAssert(m_CurrentCommandContext != CommandListContext::Invalid);
	FlushResourceBarriers();
	if (m_CurrentCommandContext == CommandListContext::Graphics)
		m_pCommandList->SetGraphicsRootShaderResourceView(rootIndex, address);
	else
		m_pCommandList->SetComputeRootShaderResourceView(rootIndex, address);
}

void CommandContext::BindRootUAV(uint32 rootIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
{
	gAssert(m_CurrentCommandContext != CommandListContext::Invalid);
	FlushResourceBarriers();
	if (m_CurrentCommandContext == CommandListContext::Graphics)
		m_pCommandList->SetGraphicsRootUnorderedAccessView(rootIndex, address);
	else
		m_pCommandList->SetComputeRootUnorderedAccessView(rootIndex, address);
}

void CommandContext::BindRootCBV(uint32 rootIndex, const void* pData, uint32 dataSize)
{
	gAssert(m_CurrentCommandContext != CommandListContext::Invalid);

	const RootSignature* pRootSignature	 = m_CurrentCommandContext == CommandListContext::Graphics ? m_pCurrentGraphicsRS : m_pCurrentComputeRS;
	bool				 isRootConstants = pRootSignature->IsRootConstant(rootIndex);
	if (isRootConstants)
	{
		gAssert(dataSize % sizeof(uint32) == 0);
		uint32 rootConstantsSize = pRootSignature->GetNumRootConstants(rootIndex) * sizeof(uint32);
		gAssert(dataSize <= rootConstantsSize);

#ifdef _DEBUG
		// In debug, write 0xCDCDCDCD to unwritten root constants
		if (rootConstantsSize != dataSize)
		{
			void* pLocalData = _alloca(rootConstantsSize);
			memset(pLocalData, (int)0xCDCDCDCD, rootConstantsSize);
			memcpy(pLocalData, pData, dataSize);
			dataSize = rootConstantsSize;
			pData	 = pLocalData;
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

		gAssert(!pRootSignature->IsRootConstant(rootIndex));
		if (m_CurrentCommandContext == CommandListContext::Graphics)
			m_pCommandList->SetGraphicsRootConstantBufferView(rootIndex, allocation.GPUAddress);
		else
			m_pCommandList->SetComputeRootConstantBufferView(rootIndex, allocation.GPUAddress);
	}
}

void CommandContext::BindRootSRV(uint32 rootIndex, const void* pData, uint32 dataSize)
{
	ScratchAllocation allocation = AllocateScratch(dataSize, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
	memcpy(allocation.pMappedMemory, pData, dataSize);
	BindRootSRV(rootIndex, allocation.GPUAddress);
}

void CommandContext::BindRootCBV(uint32 rootIndex, D3D12_GPU_VIRTUAL_ADDRESS address)
{
#if ENABLE_ASSERTS
	const RootSignature* pRootSignature = m_CurrentCommandContext == CommandListContext::Graphics ? m_pCurrentGraphicsRS : m_pCurrentComputeRS;
	gAssert(!pRootSignature->IsRootConstant(rootIndex));
#endif

	if (m_CurrentCommandContext == CommandListContext::Graphics)
		m_pCommandList->SetGraphicsRootConstantBufferView(rootIndex, address);
	else
		m_pCommandList->SetComputeRootConstantBufferView(rootIndex, address);
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
		uint32			subResource = pending.Subresource;
		DeviceResource* pResource	= pending.pResource;

		// Retrieve the last known resource state
		D3D12_RESOURCE_STATES beforeState = pResource->GetResourceState(subResource);
		gAssert(D3D::IsTransitionAllowed(m_Type, beforeState),
				"Resource (%s) can not be transitioned from this state (%s) on this queue (%s). Insert a barrier on another queue before executing this one.",
				pResource->GetName(), D3D::ResourceStateToString(beforeState).c_str(), D3D::CommandlistTypeToString(m_Type));

		// Get the after state of the first use in the current cmdlist
		D3D12_RESOURCE_STATES afterState = pending.State;
		if (D3D::NeedsTransition(beforeState, afterState, false))
			resolveContext.m_BatchedBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pResource->GetResource(), beforeState, afterState, subResource));

		// Update the resource with the last known state of the current cmdlist
		D3D12_RESOURCE_STATES end_state = GetLocalResourceState(pending.pResource, subResource);
		pResource->SetResourceState(end_state, subResource);
	}
	resolveContext.FlushResourceBarriers();
	m_PendingBarriers.clear();
}

D3D12_CPU_DESCRIPTOR_HANDLE CommandContext::GetRTV(uint32 slot, const Texture* pTexture, uint32 mipLevel, uint32 arrayIndex)
{
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	const TextureDesc&			  desc	  = pTexture->GetDesc();
	rtvDesc.Format						  = D3D::ConvertFormat(desc.Format);
	switch (desc.Type)
	{
	case TextureType::Texture1D:
		rtvDesc.Texture1D.MipSlice = mipLevel;
		rtvDesc.ViewDimension	   = D3D12_RTV_DIMENSION_TEXTURE1D;
		break;
	case TextureType::Texture1DArray:
		rtvDesc.Texture1DArray.ArraySize	   = desc.ArraySize;
		rtvDesc.Texture1DArray.FirstArraySlice = arrayIndex;
		rtvDesc.Texture1DArray.MipSlice		   = mipLevel;
		rtvDesc.ViewDimension				   = D3D12_RTV_DIMENSION_TEXTURE1DARRAY;
		break;
	case TextureType::Texture2D:
		rtvDesc.Texture2D.MipSlice	 = mipLevel;
		rtvDesc.Texture2D.PlaneSlice = 0;
		rtvDesc.ViewDimension		 = desc.SampleCount > 1 ? D3D12_RTV_DIMENSION_TEXTURE2DMS : D3D12_RTV_DIMENSION_TEXTURE2D;
		break;
	case TextureType::TextureCube:
	case TextureType::TextureCubeArray:
	case TextureType::Texture2DArray:
		rtvDesc.Texture2DArray.MipSlice		   = mipLevel;
		rtvDesc.Texture2DArray.PlaneSlice	   = 0;
		rtvDesc.Texture2DArray.ArraySize	   = desc.ArraySize;
		rtvDesc.Texture2DArray.FirstArraySlice = arrayIndex;
		rtvDesc.ViewDimension				   = desc.SampleCount > 1 ? D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY : D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
		break;
	case TextureType::Texture3D:
		rtvDesc.Texture3D.FirstWSlice = 0;
		rtvDesc.Texture3D.MipSlice	  = mipLevel;
		rtvDesc.Texture3D.WSize		  = desc.Depth;
		rtvDesc.ViewDimension		  = D3D12_RTV_DIMENSION_TEXTURE3D;
		break;
	default:
		break;
	}

	D3D12_CPU_DESCRIPTOR_HANDLE rtv = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_pRTVHeap->GetCPUDescriptorHandleForHeapStart(), slot, m_RTVSize);
	GetParent()->GetDevice()->CreateRenderTargetView(pTexture->GetResource(), &rtvDesc, rtv);
	return rtv;
}


D3D12_CPU_DESCRIPTOR_HANDLE CommandContext::GetDSV(const Texture* pTexture, RenderPassDepthFlags flags, uint32 mipLevel, uint32 arrayIndex)
{
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_pDSVHeap->GetCPUDescriptorHandleForHeapStart();

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	const TextureDesc&			  desc	  = pTexture->GetDesc();
	dsvDesc.Format						  = D3D::ConvertFormat(desc.Format);
	switch (desc.Type)
	{
	case TextureType::Texture1D:
		dsvDesc.Texture1D.MipSlice = mipLevel;
		dsvDesc.ViewDimension	   = D3D12_DSV_DIMENSION_TEXTURE1D;
		break;
	case TextureType::Texture1DArray:
		dsvDesc.Texture1DArray.ArraySize	   = desc.ArraySize;
		dsvDesc.Texture1DArray.FirstArraySlice = arrayIndex;
		dsvDesc.Texture1DArray.MipSlice		   = mipLevel;
		dsvDesc.ViewDimension				   = D3D12_DSV_DIMENSION_TEXTURE1DARRAY;
		break;
	case TextureType::Texture2D:
		dsvDesc.Texture2D.MipSlice = mipLevel;
		dsvDesc.ViewDimension	   = desc.SampleCount > 1 ? D3D12_DSV_DIMENSION_TEXTURE2DMS : D3D12_DSV_DIMENSION_TEXTURE2D;
		break;
	case TextureType::Texture3D:
	case TextureType::Texture2DArray:
		dsvDesc.Texture2DArray.ArraySize	   = desc.ArraySize;
		dsvDesc.Texture2DArray.FirstArraySlice = arrayIndex;
		dsvDesc.Texture2DArray.MipSlice		   = mipLevel;
		dsvDesc.ViewDimension				   = desc.SampleCount > 1 ? D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY : D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
		break;
	case TextureType::TextureCube:
	case TextureType::TextureCubeArray:
		dsvDesc.Texture2DArray.ArraySize	   = desc.ArraySize * 6;
		dsvDesc.Texture2DArray.FirstArraySlice = arrayIndex;
		dsvDesc.Texture2DArray.MipSlice		   = mipLevel;
		dsvDesc.ViewDimension				   = desc.SampleCount > 1 ? D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY : D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
		break;
	default:
		break;
	}
	if (EnumHasAllFlags(flags, RenderPassDepthFlags::ReadOnlyDepth))
		dsvDesc.Flags |= D3D12_DSV_FLAG_READ_ONLY_DEPTH;
	if (EnumHasAllFlags(flags, RenderPassDepthFlags::ReadOnlyStencil))
		dsvDesc.Flags |= D3D12_DSV_FLAG_READ_ONLY_STENCIL;
	GetParent()->GetDevice()->CreateDepthStencilView(pTexture->GetResource(), &dsvDesc, dsvHandle);
	return dsvHandle;
}


void CommandContext::BeginRenderPass(const RenderPassInfo& renderPassInfo)
{
	gAssert(!m_InRenderPass, "Already in RenderPass");

	FlushResourceBarriers();

	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = {};

	D3D12_CLEAR_FLAGS clearFlags = (D3D12_CLEAR_FLAGS)0;
	if (EnumHasAllFlags(renderPassInfo.DepthStencilTarget.Flags, RenderPassDepthFlags::ClearDepth))
		clearFlags |= D3D12_CLEAR_FLAGS::D3D12_CLEAR_FLAG_DEPTH;

	if (EnumHasAllFlags(renderPassInfo.DepthStencilTarget.Flags, RenderPassDepthFlags::ClearStencil))
		clearFlags |= D3D12_CLEAR_FLAGS::D3D12_CLEAR_FLAG_STENCIL;

	if (renderPassInfo.DepthStencilTarget.pTarget)
	{
		const RenderPassInfo::DepthTargetInfo& depthInfo = renderPassInfo.DepthStencilTarget;
		dsvHandle										 = GetDSV(depthInfo.pTarget, depthInfo.Flags, depthInfo.MipLevel, depthInfo.ArrayIndex);
	}

	if (clearFlags != (D3D12_CLEAR_FLAGS)0)
	{
		const ClearBinding& clearBinding = renderPassInfo.DepthStencilTarget.pTarget->GetClearBinding();
		gAssert(clearBinding.BindingValue == ClearBinding::ClearBindingValue::DepthStencil);
		m_pCommandList->ClearDepthStencilView(dsvHandle, clearFlags, clearBinding.DepthStencil.Depth, clearBinding.DepthStencil.Stencil, 0, nullptr);
	}

	StaticArray<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT> rtvs;
	for (uint32 i = 0; i < renderPassInfo.RenderTargetCount; ++i)
	{
		const RenderPassInfo::RenderTargetInfo& data = renderPassInfo.RenderTargets[i];
		D3D12_CPU_DESCRIPTOR_HANDLE				rtv	 = GetRTV(i, data.pTarget, data.MipLevel, data.ArrayIndex);

		if (EnumHasAllFlags(data.Flags, RenderPassColorFlags::Clear))
		{
			gAssert(data.pTarget->GetClearBinding().BindingValue == ClearBinding::ClearBindingValue::Color);
			m_pCommandList->ClearRenderTargetView(rtv, &data.pTarget->GetClearBinding().Color.x, 0, nullptr);
		}
		rtvs[i] = rtv;
	}
	m_pCommandList->OMSetRenderTargets(renderPassInfo.RenderTargetCount, rtvs.data(), false, dsvHandle.ptr != 0 ? &dsvHandle : nullptr);

	m_InRenderPass = true;
	m_CurrentRenderPassInfo = renderPassInfo;

	Texture* pTargetTexture = renderPassInfo.DepthStencilTarget.pTarget ? renderPassInfo.DepthStencilTarget.pTarget : renderPassInfo.RenderTargets[0].pTarget;
	if (pTargetTexture)
		SetViewport(FloatRect(0, 0, (float)pTargetTexture->GetWidth(), (float)pTargetTexture->GetHeight()), 0, 1);
}

void CommandContext::EndRenderPass()
{
	gAssert(m_InRenderPass);

	for (uint32 i = 0; i < m_CurrentRenderPassInfo.RenderTargetCount; ++i)
	{
		const RenderPassInfo::RenderTargetInfo& data = m_CurrentRenderPassInfo.RenderTargets[i];
		if (EnumHasAllFlags(data.Flags, RenderPassColorFlags::Resolve))
		{
			if (data.pTarget->GetDesc().SampleCount > 1)
			{
				InsertResourceBarrier(data.pTarget, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
				InsertResourceBarrier(data.pResolveTarget, D3D12_RESOURCE_STATE_UNKNOWN, D3D12_RESOURCE_STATE_RESOLVE_DEST);
				uint32 subResource = D3D12CalcSubresource(data.MipLevel, data.ArrayIndex, 0, data.pTarget->GetMipLevels(), data.pTarget->GetArraySize());
				ResolveResource(data.pTarget, subResource, data.pResolveTarget, 0, data.pTarget->GetFormat());
			}
			else if (data.pTarget != data.pResolveTarget)
			{
				E_LOG(Warning, "RenderTarget %u is set to resolve but has a sample count of 1. This will just do a CopyTexture instead which is wasteful.", i);
				CopyResource(data.pTarget, data.pResolveTarget);
			}
		}
	}

	m_InRenderPass = false;
}

void CommandContext::Draw(uint32 vertexStart, uint32 vertexCount, uint32 instances, uint32 instanceStart)
{
	gAssert(m_pCurrentPSO);
	gAssert(m_CurrentCommandContext == CommandListContext::Graphics);
	PrepareDraw();
	m_pCommandList->DrawInstanced(vertexCount, instances, vertexStart, instanceStart);
}

void CommandContext::DrawIndexedInstanced(uint32 indexCount, uint32 indexStart, uint32 instanceCount, uint32 minVertex /*= 0*/, uint32 instanceStart /*= 0*/)
{
	gAssert(m_pCurrentPSO);
	gAssert(m_CurrentCommandContext == CommandListContext::Graphics);
	PrepareDraw();
	m_pCommandList->DrawIndexedInstanced(indexCount, instanceCount, indexStart, minVertex, instanceStart);
}

void CommandContext::DispatchRays(ShaderBindingTable& table, uint32 width /*= 1*/, uint32 height /*= 1*/, uint32 depth /*= 1*/)
{
	gAssert(m_pCurrentSO);
	gAssert(m_CurrentCommandContext == CommandListContext::Compute);
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
	gAssert(m_CurrentCommandContext == CommandListContext::Compute);
	PrepareDraw();
	m_pCommandList->DispatchGraph(&graphDesc);
}

void CommandContext::ResolveResource(Texture* pSource, uint32 sourceSubResource, Texture* pTarget, uint32 targetSubResource, ResourceFormat format)
{
	FlushResourceBarriers();
	m_pCommandList->ResolveSubresource(pTarget->GetResource(), targetSubResource, pSource->GetResource(), sourceSubResource, D3D::ConvertFormat(format));
}

void CommandContext::AddBarrier(const D3D12_RESOURCE_BARRIER& inBarrier)
{
	m_BatchedBarriers.push_back(inBarrier);
}

void CommandContext::PrepareDraw()
{
	gAssert(m_CurrentCommandContext != CommandListContext::Invalid);
	FlushResourceBarriers();
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
	gAssert(buffers.GetSize() < MAX_VERTEX_BUFFERS, "VertexBuffer count (%d) exceeds the maximum (%d)", buffers.GetSize(), MAX_VERTEX_BUFFERS);
	D3D12_VERTEX_BUFFER_VIEW views[MAX_VERTEX_BUFFERS];

	uint32 numViews = 0;
	for (const VertexBufferView& view : buffers)
	{
		views[numViews] = {
			.BufferLocation = view.Location,
			.SizeInBytes	= view.Elements * view.Stride,
			.StrideInBytes	= view.Stride,
		};
		++numViews;
	}
	m_pCommandList->IASetVertexBuffers(0, buffers.GetSize(), views);
}

void CommandContext::SetIndexBuffer(const IndexBufferView& indexBuffer)
{
	D3D12_INDEX_BUFFER_VIEW view ={
		.BufferLocation	= indexBuffer.Location,
		.SizeInBytes	= indexBuffer.Stride() * indexBuffer.Elements,
		.Format			= D3D::ConvertFormat(indexBuffer.Format),
	};
	m_pCommandList->IASetIndexBuffer(&view);
}

void CommandContext::SetViewport(const FloatRect& rect, float minDepth /*= 0.0f*/, float maxDepth /*= 1.0f*/)
{
	D3D12_VIEWPORT viewport = {
		.TopLeftX	= (float)rect.Left,
		.TopLeftY	= (float)rect.Top,
		.Width		= (float)rect.GetWidth(),
		.Height		= (float)rect.GetHeight(),
		.MinDepth	= minDepth,
		.MaxDepth	= maxDepth,
	};
	m_pCommandList->RSSetViewports(1, &viewport);
	SetScissorRect(rect);
}


void CommandContext::BindDynamicVertexBuffer(uint32 rootIndex, uint32 elementCount, uint32 elementSize, const void* pData)
{
	uint32 bufferSize = elementCount * elementSize;
	ScratchAllocation allocation = AllocateScratch(bufferSize);
	memcpy(allocation.pMappedMemory, pData, bufferSize);
	D3D12_VERTEX_BUFFER_VIEW view = {
		.BufferLocation = allocation.GPUAddress,
		.SizeInBytes	= bufferSize,
		.StrideInBytes	= elementSize,
	};
	m_pCommandList->IASetVertexBuffers(rootIndex, 1, &view);
}

void CommandContext::BindDynamicIndexBuffer(uint32 elementCount, const void* pData, ResourceFormat format)
{
	uint32 bufferSize = (uint32)RHI::GetRowPitch(format, elementCount);
	ScratchAllocation allocation = AllocateScratch(bufferSize);
	memcpy(allocation.pMappedMemory, pData, bufferSize);
	D3D12_INDEX_BUFFER_VIEW view = {
		.BufferLocation	= allocation.GPUAddress,
		.SizeInBytes	= bufferSize,
		.Format			= D3D::ConvertFormat(format),
	};
	m_pCommandList->IASetIndexBuffer(&view);
}

void CommandContext::SetScissorRect(const FloatRect& rect)
{
	D3D12_RECT r = {
		.left	= (LONG)rect.Left,
		.top	= (LONG)rect.Top,
		.right	= (LONG)rect.Right,
		.bottom	= (LONG)rect.Bottom,
	};
	m_pCommandList->RSSetScissorRects(1, &r);
}

void CommandContext::SetStencilRef(uint32 stencilRef)
{
	m_pCommandList->OMSetStencilRef(stencilRef);
}

CommandSignature::CommandSignature(GraphicsDevice* pParent, ID3D12CommandSignature* pCmdSignature)
	: DeviceObject(pParent), m_pCommandSignature(pCmdSignature)
{
}

D3D12_COMMAND_SIGNATURE_DESC CommandSignatureInitializer::GetDesc() const
{
	return D3D12_COMMAND_SIGNATURE_DESC{
		.ByteStride			= m_Stride,
		.NumArgumentDescs	= (uint32)m_ArgumentDesc.size(),
		.pArgumentDescs		= m_ArgumentDesc.data(),
		.NodeMask			= 0,
	};
}

void CommandSignatureInitializer::AddDispatch()
{
	m_ArgumentDesc.push_back({ .Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH });
	m_Stride += sizeof(D3D12_DISPATCH_ARGUMENTS);
}

void CommandSignatureInitializer::AddDispatchMesh()
{
	m_ArgumentDesc.push_back({ .Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH });
	m_Stride += sizeof(D3D12_DISPATCH_MESH_ARGUMENTS);
}

void CommandSignatureInitializer::AddDraw()
{
	m_ArgumentDesc.push_back({ .Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW });
	m_Stride += sizeof(D3D12_DRAW_ARGUMENTS);
}

void CommandSignatureInitializer::AddDrawIndexed()
{
	m_ArgumentDesc.push_back({ .Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED });
	m_Stride += sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
}

void CommandSignatureInitializer::AddConstants(uint32 numConstants, uint32 rootIndex, uint32 offset)
{
	m_ArgumentDesc.push_back({
		.Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT,
		.Constant {
			.RootParameterIndex			= rootIndex,
			.DestOffsetIn32BitValues	= offset,
			.Num32BitValuesToSet		= numConstants,
		}
	});
	m_Stride += numConstants * sizeof(uint32);
}

void CommandSignatureInitializer::AddConstantBufferView(uint32 rootIndex)
{
	m_ArgumentDesc.push_back({
		.Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW,
		.ConstantBufferView {
			.RootParameterIndex = rootIndex,
		}
	});
	m_Stride += sizeof(uint64);
}

void CommandSignatureInitializer::AddShaderResourceView(uint32 rootIndex)
{
	m_ArgumentDesc.push_back({
		.Type = D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW,
		.ShaderResourceView {
			.RootParameterIndex = rootIndex,
		}
	});
	m_Stride += 8;
}

void CommandSignatureInitializer::AddUnorderedAccessView(uint32 rootIndex)
{
	m_ArgumentDesc.push_back({
		.Type = D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW,
		.UnorderedAccessView {
			.RootParameterIndex = rootIndex,
		}
	});
	m_Stride += 8;
}

void CommandSignatureInitializer::AddVertexBuffer(uint32 slot)
{
	m_ArgumentDesc.push_back({
		.Type = D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW,
		.VertexBuffer {
			.Slot = slot,
		}
	});
	m_Stride += sizeof(D3D12_VERTEX_BUFFER_VIEW);
}

void CommandSignatureInitializer::AddIndexBuffer()
{
	m_ArgumentDesc.push_back({ .Type = D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW });
	m_Stride += sizeof(D3D12_INDEX_BUFFER_VIEW);
}
