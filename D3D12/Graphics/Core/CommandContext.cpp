#include "stdafx.h"
#include "CommandContext.h"
#include "Graphics.h"
#include "CommandQueue.h"
#include "DynamicResourceAllocator.h"
#include "OnlineDescriptorAllocator.h"
#include "PipelineState.h"
#include "RootSignature.h"
#include "GraphicsBuffer.h"
#include "Texture.h"
#include "ResourceViews.h"
#include "CommandSignature.h"
#include "RaytracingCommon.h"

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

CommandContext::CommandContext(Graphics* pGraphics, ID3D12GraphicsCommandList* pCommandList, ID3D12CommandAllocator* pAllocator, D3D12_COMMAND_LIST_TYPE type)
	: GraphicsObject(pGraphics), m_pCommandList(pCommandList), m_pAllocator(pAllocator), m_Type(type)
{
	m_DynamicAllocator = std::make_unique<DynamicResourceAllocator>(pGraphics->GetAllocationManager());
	if (m_Type != D3D12_COMMAND_LIST_TYPE_COPY)
	{
		m_pShaderResourceDescriptorAllocator = std::make_unique<OnlineDescriptorAllocator>(pGraphics, this, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		m_pSamplerDescriptorAllocator = std::make_unique<OnlineDescriptorAllocator>(pGraphics, this, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
	}
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
		m_pAllocator = m_pGraphics->GetCommandQueue(m_Type)->RequestAllocator();
		m_pCommandList->Reset(m_pAllocator, nullptr);
	}
	m_BarrierBatcher.Reset();
	BindDescriptorHeaps();
}

uint64 CommandContext::Execute(bool wait)
{
	FlushResourceBarriers();
	CommandQueue* pQueue = m_pGraphics->GetCommandQueue(m_Type);
	uint64 fenceValue = pQueue->ExecuteCommandList(m_pCommandList);

	if (wait)
	{
		pQueue->WaitForFence(fenceValue);
	}

	m_DynamicAllocator->Free(fenceValue);
	pQueue->FreeAllocator(fenceValue, m_pAllocator);
	m_pAllocator = nullptr;
	m_pGraphics->FreeCommandList(this);

	if (m_pShaderResourceDescriptorAllocator)
	{
		m_pShaderResourceDescriptorAllocator->ReleaseUsedHeaps(fenceValue);
	}
	if (m_pSamplerDescriptorAllocator)
	{
		m_pSamplerDescriptorAllocator->ReleaseUsedHeaps(fenceValue);
	}

	return fenceValue;
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

void CommandContext::InsertResourceBarrier(GraphicsResource* pBuffer, D3D12_RESOURCE_STATES state, uint32 subResource /*= 0xffffffff*/)
{
	D3D12_RESOURCE_STATES beforeState = pBuffer->GetResourceState();
	if (m_Type == D3D12_COMMAND_LIST_TYPE_COMPUTE)
	{
		check((beforeState & VALID_COMPUTE_QUEUE_RESOURCE_STATES) == beforeState);
		check((state & VALID_COMPUTE_QUEUE_RESOURCE_STATES) == state);
	}
	else if (m_Type == D3D12_COMMAND_LIST_TYPE_COPY)
	{
		check((beforeState & VALID_COPY_QUEUE_RESOURCE_STATES) == beforeState);
		check((state & VALID_COPY_QUEUE_RESOURCE_STATES) == state);
	}

	if (NeedsTransition(beforeState, state))
	{
		m_BarrierBatcher.AddTransition(pBuffer->GetResource(), pBuffer->GetResourceState(subResource), state, subResource);
		pBuffer->SetResourceState(state, subResource);
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
	check(pSource && pTarget);
	D3D12_RESOURCE_STATES sourceState = pSource->GetResourceState();
	D3D12_RESOURCE_STATES targetState = pTarget->GetResourceState();
	InsertResourceBarrier(pSource, D3D12_RESOURCE_STATE_COPY_SOURCE);
	InsertResourceBarrier(pTarget, D3D12_RESOURCE_STATE_COPY_DEST);
	FlushResourceBarriers();
	m_pCommandList->CopyResource(pTarget->GetResource(), pSource->GetResource());
	InsertResourceBarrier(pSource, sourceState);
	InsertResourceBarrier(pTarget, targetState);
}

void CommandContext::CopyTexture(Texture* pSource, Buffer* pDestination, const D3D12_BOX& sourceRegion, int sourceSubregion /*= 0*/, int destinationOffset /*= 0*/)
{
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT bufferFootprint = {};
	bufferFootprint.Footprint.Width = (uint32)pDestination->GetSize();
	bufferFootprint.Footprint.Height = 1;
	bufferFootprint.Footprint.Depth = 1;
	bufferFootprint.Footprint.RowPitch = Math::AlignUp<uint32>((uint32)pDestination->GetSize(), D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
	bufferFootprint.Footprint.Format = pDestination->GetDesc().Format;
	bufferFootprint.Offset = 0;

	CD3DX12_TEXTURE_COPY_LOCATION srcLocation(pSource->GetResource(), sourceSubregion);
	CD3DX12_TEXTURE_COPY_LOCATION dstLocation(pDestination->GetResource(), bufferFootprint);
	m_pCommandList->CopyTextureRegion(&dstLocation, destinationOffset, 0, 0, &srcLocation, &sourceRegion);
}

void CommandContext::CopyTexture(Texture* pSource, Texture* pDestination, const D3D12_BOX& sourceRegion, const D3D12_BOX& destinationRegion, int sourceSubregion /*= 0*/, int destinationSubregion /*= 0*/)
{
	CD3DX12_TEXTURE_COPY_LOCATION srcLocation(pSource->GetResource(), sourceSubregion);
	CD3DX12_TEXTURE_COPY_LOCATION dstLocation(pDestination->GetResource(), destinationSubregion);
	m_pCommandList->CopyTextureRegion(&dstLocation, destinationRegion.left, destinationRegion.top, destinationRegion.front, &srcLocation, &sourceRegion);
}

void CommandContext::CopyBuffer(Buffer* pSource, Buffer* pDestination, uint32 size, uint32 sourceOffset, uint32 destinationOffset)
{
	m_pCommandList->CopyBufferRegion(pDestination->GetResource(), destinationOffset, pSource->GetResource(), sourceOffset, size);
}

void CommandContext::InitializeBuffer(Buffer* pResource, const void* pData, uint64 dataSize, uint64 offset)
{
	DynamicAllocation allocation = m_DynamicAllocator->Allocate(dataSize);
	memcpy(allocation.pMappedMemory, pData, dataSize);
	D3D12_RESOURCE_STATES previousState = pResource->GetResourceState();
	InsertResourceBarrier(pResource, D3D12_RESOURCE_STATE_COPY_DEST);
	FlushResourceBarriers();
	m_pCommandList->CopyBufferRegion(pResource->GetResource(), offset, allocation.pBackingResource->GetResource(), allocation.Offset, dataSize);
	InsertResourceBarrier(pResource, previousState);
	FlushResourceBarriers();
}

void CommandContext::InitializeTexture(Texture* pResource, D3D12_SUBRESOURCE_DATA* pSubResourceDatas, int firstSubResource, int subResourceCount)
{
	D3D12_RESOURCE_DESC desc = pResource->GetResource()->GetDesc();
	uint64 requiredSize = 0;
	m_pGraphics->GetDevice()->GetCopyableFootprints(&desc, firstSubResource, subResourceCount, 0, nullptr, nullptr, nullptr, &requiredSize);
	DynamicAllocation allocation = m_DynamicAllocator->Allocate(requiredSize, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
	D3D12_RESOURCE_STATES previousState = pResource->GetResourceState();
	InsertResourceBarrier(pResource, D3D12_RESOURCE_STATE_COPY_DEST);
	FlushResourceBarriers();
	UpdateSubresources(m_pCommandList, pResource->GetResource(), allocation.pBackingResource->GetResource(), allocation.Offset, firstSubResource, subResourceCount, pSubResourceDatas);
	InsertResourceBarrier(pResource, previousState);
	FlushResourceBarriers();
}

void CommandContext::Dispatch(uint32 groupCountX, uint32 groupCountY, uint32 groupCountZ)
{
	PrepareDraw(DescriptorTableType::Compute);
	m_pCommandList->Dispatch(groupCountX, groupCountY, groupCountZ);
}

void CommandContext::Dispatch(const IntVector3& groupCounts)
{
	Dispatch(groupCounts.x, groupCounts.y, groupCounts.z);
}

void CommandContext::DispatchMesh(uint32 groupCountX, uint32 groupCountY /*= 1*/, uint32 groupCountZ /*= 1*/)
{
	check(m_pMeshShadingCommandList);
	PrepareDraw(DescriptorTableType::Graphics);
	m_pMeshShadingCommandList->DispatchMesh(groupCountX, groupCountY, groupCountZ);
}

void CommandContext::ExecuteIndirect(CommandSignature* pCommandSignature, Buffer* pIndirectArguments, bool isCompute)
{
	PrepareDraw(isCompute ? DescriptorTableType::Compute : DescriptorTableType::Graphics);
	m_pCommandList->ExecuteIndirect(pCommandSignature->GetCommandSignature(), 1, pIndirectArguments->GetResource(), 0, nullptr, 0);
}

void CommandContext::ClearUavUInt(GraphicsResource* pBuffer, UnorderedAccessView* pUav, uint32* values /*= nullptr*/)
{
	FlushResourceBarriers();
	DescriptorHandle gpuHandle = m_pShaderResourceDescriptorAllocator->AllocateTransientDescriptor(1);
	m_pGraphics->GetDevice()->CopyDescriptorsSimple(1, gpuHandle.GetCpuHandle(), pUav->GetDescriptor(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	uint32 zeros[4] = { 0,0,0,0 };
	m_pCommandList->ClearUnorderedAccessViewUint(gpuHandle.GetGpuHandle(), pUav->GetDescriptor(), pBuffer->GetResource(), values ? values : zeros, 0, nullptr);
}

void CommandContext::ClearUavFloat(GraphicsResource* pBuffer, UnorderedAccessView* pUav, float* values /*= nullptr*/)
{
	FlushResourceBarriers();
	DescriptorHandle gpuHandle = m_pShaderResourceDescriptorAllocator->AllocateTransientDescriptor(1);
	m_pGraphics->GetDevice()->CopyDescriptorsSimple(1, gpuHandle.GetCpuHandle(), pUav->GetDescriptor(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	float zeros[4] = { 0,0,0,0 };
	m_pCommandList->ClearUnorderedAccessViewFloat(gpuHandle.GetGpuHandle(), pUav->GetDescriptor(), pBuffer->GetResource(), values ? values : zeros, 0, nullptr);
}

void CommandContext::SetComputeRootSignature(RootSignature* pRootSignature)
{
	m_pCommandList->SetComputeRootSignature(pRootSignature->GetRootSignature());
	m_pShaderResourceDescriptorAllocator->ParseRootSignature(pRootSignature);
	m_pSamplerDescriptorAllocator->ParseRootSignature(pRootSignature);
}

void CommandContext::SetComputeRootConstants(int rootIndex, uint32 count, const void* pConstants)
{
	m_pCommandList->SetComputeRoot32BitConstants(rootIndex, count, pConstants, 0);
}

void CommandContext::SetComputeDynamicConstantBufferView(int rootIndex, void* pData, uint32 dataSize)
{
	DynamicAllocation allocation = m_DynamicAllocator->Allocate(dataSize);
	memcpy(allocation.pMappedMemory, pData, dataSize);
	m_pCommandList->SetComputeRootConstantBufferView(rootIndex, allocation.GpuHandle);
}

void CommandContext::SetDynamicDescriptor(int rootIndex, int offset, D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
	m_pShaderResourceDescriptorAllocator->SetDescriptors(rootIndex, offset, 1, &handle);
}

void CommandContext::SetDynamicDescriptor(int rootIndex, int offset, UnorderedAccessView* pView)
{
	SetDynamicDescriptor(rootIndex, offset, pView->GetDescriptor());
}

void CommandContext::SetDynamicDescriptor(int rootIndex, int offset, ShaderResourceView* pView)
{
	SetDynamicDescriptor(rootIndex, offset, pView->GetDescriptor());
}

void CommandContext::SetDynamicDescriptors(int rootIndex, int offset, D3D12_CPU_DESCRIPTOR_HANDLE* handles, int count)
{
	m_pShaderResourceDescriptorAllocator->SetDescriptors(rootIndex, offset, count, handles);
}

void CommandContext::SetDynamicSampler(int rootIndex, int offset, D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
	m_pSamplerDescriptorAllocator->SetDescriptors(rootIndex, offset, 1, &handle);
}

void CommandContext::SetDynamicSamplers(int rootIndex, int offset, D3D12_CPU_DESCRIPTOR_HANDLE* handles, int count)
{
	m_pSamplerDescriptorAllocator->SetDescriptors(rootIndex, offset, count, handles);
}

void CommandContext::SetDescriptorHeap(ID3D12DescriptorHeap* pHeap, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
	if (m_CurrentDescriptorHeaps[(int)type] != pHeap)
	{
		m_CurrentDescriptorHeaps[(int)type] = pHeap;
		BindDescriptorHeaps();
	}
}

DynamicAllocation CommandContext::AllocateTransientMemory(uint64 size)
{
	return m_DynamicAllocator->Allocate(size);
}

DescriptorHandle CommandContext::AllocateTransientDescriptors(int descriptorCount, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
	return m_pShaderResourceDescriptorAllocator->AllocateTransientDescriptor(descriptorCount);
}

void CommandContext::BindDescriptorHeaps()
{
	std::array<ID3D12DescriptorHeap*, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES> heapsToBind = {};
	int heapCount = 0;
	for (size_t i = 0; i < heapsToBind.size(); ++i)
	{
		if (m_CurrentDescriptorHeaps[i] != nullptr)
		{
			heapsToBind[heapCount] = m_CurrentDescriptorHeaps[i];
			++heapCount;
		}
	}
	if (heapCount > 0)
	{
		m_pCommandList->SetDescriptorHeaps(heapCount, heapsToBind.data());
	}
}

void CommandContext::BeginRenderPass(const RenderPassInfo& renderPassInfo)
{
	check(!m_InRenderPass);
	check(renderPassInfo.DepthStencilTarget.Target || (renderPassInfo.DepthStencilTarget.Access == RenderPassAccess::NoAccess && renderPassInfo.DepthStencilTarget.StencilAccess == RenderPassAccess::NoAccess));

	auto ExtractBeginAccess = [](RenderPassAccess access) -> D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE
	{
		switch (RenderPassInfo::GetBeginAccess(access))
		{
		case RenderTargetLoadAction::DontCare: return D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_DISCARD;
		case RenderTargetLoadAction::Load: return D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE;
		case RenderTargetLoadAction::Clear: return D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
		case RenderTargetLoadAction::NoAccess: return D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_NO_ACCESS;
		}
		check(false);
		return D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_NO_ACCESS;
	};

	auto ExtractEndingAccess = [](RenderPassAccess access) -> D3D12_RENDER_PASS_ENDING_ACCESS_TYPE
	{
		switch (RenderPassInfo::GetEndAccess(access))
		{
		case RenderTargetStoreAction::DontCare: return D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_DISCARD;
		case RenderTargetStoreAction::Store: return D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;
		case RenderTargetStoreAction::Resolve: return D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_RESOLVE;
		case RenderTargetStoreAction::NoAccess: return D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_NO_ACCESS;
		}
		check(false);
		return D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_NO_ACCESS;
	};

#if D3D12_USE_RENDERPASSES
	ComPtr<ID3D12GraphicsCommandList4> pCmd;
	if (m_pGraphics->UseRenderPasses() && m_pCommandList->QueryInterface(IID_PPV_ARGS(pCmd.GetAddressOf())) == S_OK)
	{
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
		bool writeable = true;
		if (renderPassDepthStencilDesc.DepthEndingAccess.Type == D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_DISCARD)
		{
			writeable = false;
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
		if (renderPassInfo.DepthStencilTarget.Target != nullptr)
		{
			renderPassDepthStencilDesc.cpuDescriptor = renderPassInfo.DepthStencilTarget.Target->GetDSV(writeable);
		}

		std::array<D3D12_RENDER_PASS_RENDER_TARGET_DESC, 4> renderTargetDescs{};
		for (uint32 i = 0; i < renderPassInfo.RenderTargetCount; ++i)
		{
			const RenderPassInfo::RenderTargetInfo& data = renderPassInfo.RenderTargets[i];

			renderTargetDescs[i].BeginningAccess.Type = ExtractBeginAccess(data.Access);

			if (renderTargetDescs[i].BeginningAccess.Type == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR)
			{
				check(data.Target->GetClearBinding().BindingValue == ClearBinding::ClearBindingValue::Color);
				memcpy(renderTargetDescs[i].BeginningAccess.Clear.ClearValue.Color, &data.Target->GetClearBinding().Color, sizeof(Color));
				renderTargetDescs[i].BeginningAccess.Clear.ClearValue.Format = data.Target->GetFormat();
			}
			renderTargetDescs[i].EndingAccess.Type = ExtractEndingAccess(data.Access);

			uint32 subResource = D3D12CalcSubresource(data.MipLevel, data.ArrayIndex, 0, data.Target->GetMipLevels(), data.Target->GetArraySize());

			std::array<D3D12_RENDER_PASS_ENDING_ACCESS_RESOLVE_SUBRESOURCE_PARAMETERS, 4> subResourceParameters{};

			if (renderTargetDescs[i].EndingAccess.Type == D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_RESOLVE)
			{
				check(data.ResolveTarget);
				InsertResourceBarrier(data.ResolveTarget, D3D12_RESOURCE_STATE_RESOLVE_DEST);
				renderTargetDescs[i].EndingAccess.Resolve.Format = data.Target->GetFormat();
				renderTargetDescs[i].EndingAccess.Resolve.pDstResource = data.ResolveTarget->GetResource();
				renderTargetDescs[i].EndingAccess.Resolve.pSrcResource = data.Target->GetResource();
				renderTargetDescs[i].EndingAccess.Resolve.PreserveResolveSource = false;
				renderTargetDescs[i].EndingAccess.Resolve.ResolveMode = D3D12_RESOLVE_MODE_AVERAGE;
				renderTargetDescs[i].EndingAccess.Resolve.SubresourceCount = 1;
				subResourceParameters[i].DstSubresource = 0;
				subResourceParameters[i].SrcSubresource = subResource;
				subResourceParameters[i].DstX = 0;
				subResourceParameters[i].DstY = 0;
				renderTargetDescs[i].EndingAccess.Resolve.pSubresourceParameters = subResourceParameters.data();
			}

			renderTargetDescs[i].cpuDescriptor = data.Target->GetRTV();
		}

		D3D12_RENDER_PASS_FLAGS renderPassFlags = D3D12_RENDER_PASS_FLAG_NONE;
		if (renderPassInfo.WriteUAVs)
		{
			renderPassFlags |= D3D12_RENDER_PASS_FLAG_ALLOW_UAV_WRITES;
		}

		FlushResourceBarriers();
		pCmd->BeginRenderPass(renderPassInfo.RenderTargetCount, renderTargetDescs.data(), renderPassInfo.DepthStencilTarget.Target ? &renderPassDepthStencilDesc : nullptr, renderPassFlags);
	}
	else
#endif
	{
		FlushResourceBarriers();
		bool writeable = true;
		if (ExtractEndingAccess(renderPassInfo.DepthStencilTarget.Access) == D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_DISCARD)
		{
			writeable = false;
		}
		D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = renderPassInfo.DepthStencilTarget.Target ? renderPassInfo.DepthStencilTarget.Target->GetDSV(writeable) : D3D12_CPU_DESCRIPTOR_HANDLE{};
		D3D12_CLEAR_FLAGS clearFlags = (D3D12_CLEAR_FLAGS)0;
		if (ExtractBeginAccess(renderPassInfo.DepthStencilTarget.Access) == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR)
		{
			clearFlags |= D3D12_CLEAR_FLAGS::D3D12_CLEAR_FLAG_DEPTH;
		}
		if (ExtractBeginAccess(renderPassInfo.DepthStencilTarget.StencilAccess) == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR)
		{
			clearFlags |= D3D12_CLEAR_FLAGS::D3D12_CLEAR_FLAG_STENCIL;
		}
		if (clearFlags != (D3D12_CLEAR_FLAGS)0)
		{
			const ClearBinding& clearBinding = renderPassInfo.DepthStencilTarget.Target->GetClearBinding();
			check(clearBinding.BindingValue == ClearBinding::ClearBindingValue::DepthStencil);
			m_pCommandList->ClearDepthStencilView(dsvHandle, clearFlags, clearBinding.DepthStencil.Depth, clearBinding.DepthStencil.Stencil, 0, nullptr);
		}

		std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 4> rtvs;
		for (uint32 i = 0; i < renderPassInfo.RenderTargetCount; ++i)
		{
			const RenderPassInfo::RenderTargetInfo& data = renderPassInfo.RenderTargets[i];
			rtvs[i] = data.Target->GetRTV();
			
			if (ExtractBeginAccess(data.Access) == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR)
			{
				check(data.Target->GetClearBinding().BindingValue == ClearBinding::ClearBindingValue::Color);
				m_pCommandList->ClearRenderTargetView(data.Target->GetRTV(), &data.Target->GetClearBinding().Color.x, 0, nullptr);
			}
		}
		m_pCommandList->OMSetRenderTargets(renderPassInfo.RenderTargetCount, rtvs.data(), false, dsvHandle.ptr != 0 ? &dsvHandle : nullptr);
	}
	m_InRenderPass = true;
	m_CurrentRenderPassInfo = renderPassInfo;
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

#if D3D12_USE_RENDERPASSES
	ComPtr<ID3D12GraphicsCommandList4> pCmd;

	if (m_pGraphics->UseRenderPasses() && m_pCommandList->QueryInterface(IID_PPV_ARGS(pCmd.GetAddressOf())) == S_OK)
	{
		pCmd->EndRenderPass();
	}
	else
#endif
	{
		for (uint32 i = 0; i < m_CurrentRenderPassInfo.RenderTargetCount; ++i)
		{
			const RenderPassInfo::RenderTargetInfo& data = m_CurrentRenderPassInfo.RenderTargets[i];
			if (ExtractEndingAccess(data.Access) == D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_RESOLVE)
			{
				if (data.Target->GetDesc().SampleCount > 1)
				{
					InsertResourceBarrier(data.Target, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
					InsertResourceBarrier(data.ResolveTarget, D3D12_RESOURCE_STATE_RESOLVE_DEST);
					uint32 subResource = D3D12CalcSubresource(data.MipLevel, data.ArrayIndex, 0, data.Target->GetMipLevels(), data.Target->GetArraySize());
					ResolveResource(data.Target, subResource, data.ResolveTarget, 0, data.Target->GetFormat());
				}
				else
				{
					FlushResourceBarriers();
					CopyTexture(data.Target, data.ResolveTarget);
					FlushResourceBarriers();
				}
			}
		}
	}
	m_InRenderPass = false;
}

void CommandContext::Draw(int vertexStart, int vertexCount)
{
	PrepareDraw(DescriptorTableType::Graphics);
	m_pCommandList->DrawInstanced(vertexCount, 1, vertexStart, 0);
}

void CommandContext::DrawIndexed(int indexCount, int indexStart, int minVertex /*= 0*/)
{
	PrepareDraw(DescriptorTableType::Graphics);
	m_pCommandList->DrawIndexedInstanced(indexCount, 1, indexStart, minVertex, 0);
}

void CommandContext::DrawIndexedInstanced(int indexCount, int indexStart, int instanceCount, int minVertex /*= 0*/, int instanceStart /*= 0*/)
{
	PrepareDraw(DescriptorTableType::Graphics);
	m_pCommandList->DrawIndexedInstanced(indexCount, instanceCount, indexStart, minVertex, instanceStart);
}

void CommandContext::DispatchRays(ShaderBindingTable& table, uint32 width /*= 1*/, uint32 height /*= 1*/, uint32 depth /*= 1*/)
{
	check(m_pRaytracingCommandList);
	D3D12_DISPATCH_RAYS_DESC desc{};
	table.Commit(*this, desc);
	desc.Width = width;
	desc.Height = height;
	desc.Depth = depth;
	PrepareDraw(DescriptorTableType::Compute);
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

void CommandContext::PrepareDraw(DescriptorTableType type)
{
	FlushResourceBarriers();
	m_pShaderResourceDescriptorAllocator->UploadAndBindStagedDescriptors(type);
	m_pSamplerDescriptorAllocator->UploadAndBindStagedDescriptors(type);

}

void CommandContext::SetPipelineState(PipelineState* pPipelineState)
{
	m_pCommandList->SetPipelineState(pPipelineState->GetPipelineState());
}

void CommandContext::SetPipelineState(ID3D12StateObject* pStateObject)
{
	check(m_pRaytracingCommandList);
	m_pRaytracingCommandList->SetPipelineState1(pStateObject);
}

void CommandContext::SetGraphicsRootSignature(RootSignature* pRootSignature)
{
	m_pCommandList->SetGraphicsRootSignature(pRootSignature->GetRootSignature());
	m_pShaderResourceDescriptorAllocator->ParseRootSignature(pRootSignature);
	m_pSamplerDescriptorAllocator->ParseRootSignature(pRootSignature);
}

void CommandContext::SetGraphicsRootConstants(int rootIndex, uint32 count, const void* pConstants)
{
	m_pCommandList->SetGraphicsRoot32BitConstants(rootIndex, count, pConstants, 0);
}

void CommandContext::SetDynamicConstantBufferView(int rootIndex, const void* pData, uint32 dataSize)
{
	DynamicAllocation allocation = m_DynamicAllocator->Allocate(dataSize);
	memcpy(allocation.pMappedMemory, pData, dataSize);
	m_pCommandList->SetGraphicsRootConstantBufferView(rootIndex, allocation.GpuHandle);
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

void CommandContext::SetVertexBuffer(Buffer* pVertexBuffer)
{
	SetVertexBuffers(&pVertexBuffer, 1);
}

void CommandContext::SetVertexBuffers(Buffer** pVertexBuffers, int bufferCount)
{
	check(bufferCount <= 4);
	std::array<D3D12_VERTEX_BUFFER_VIEW, 4> views = {};
	for (int i = 0; i < bufferCount; ++i)
	{
		views[i].BufferLocation = pVertexBuffers[i]->GetGpuHandle();
		views[i].SizeInBytes = (uint32)pVertexBuffers[i]->GetSize();
		views[i].StrideInBytes = pVertexBuffers[i]->GetDesc().ElementSize;
	}
	m_pCommandList->IASetVertexBuffers(0, bufferCount, views.data());
}

void CommandContext::SetIndexBuffer(Buffer* pIndexBuffer)
{
	D3D12_INDEX_BUFFER_VIEW view;
	view.BufferLocation = pIndexBuffer->GetGpuHandle();
	view.Format = pIndexBuffer->GetDesc().ElementSize == 4 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
	view.SizeInBytes = (uint32)pIndexBuffer->GetSize();
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
