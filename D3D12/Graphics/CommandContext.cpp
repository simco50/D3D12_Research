#include "stdafx.h"
#include "CommandContext.h"
#include "Graphics.h"
#include "CommandQueue.h"
#include "DynamicResourceAllocator.h"
#include "DynamicDescriptorAllocator.h"
#include "PipelineState.h"
#include "RootSignature.h"
#include "GraphicsBuffer.h"
#include "Texture.h"

#include "d3dx12.h"
#include "Profiler.h"

constexpr int VALID_COMPUTE_QUEUE_RESOURCE_STATES = D3D12_RESOURCE_STATE_COMMON | D3D12_RESOURCE_STATE_UNORDERED_ACCESS | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_COPY_DEST | D3D12_RESOURCE_STATE_COPY_SOURCE | D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
constexpr int VALID_COPY_QUEUE_RESOURCE_STATES = D3D12_RESOURCE_STATE_COMMON | D3D12_RESOURCE_STATE_COPY_DEST | D3D12_RESOURCE_STATE_COPY_SOURCE;

#define USE_RENDERPASSES 0

#ifndef USE_RENDERPASSES
#define USE_RENDERPASSES 0
#endif

#pragma region BASE

CommandContext::CommandContext(Graphics* pGraphics, ID3D12GraphicsCommandList* pCommandList, ID3D12CommandAllocator* pAllocator)
	: m_pGraphics(pGraphics), m_pCommandList(pCommandList), m_pAllocator(pAllocator)
{
	m_DynamicAllocator = std::make_unique<DynamicResourceAllocator>(pGraphics->GetAllocationManager());
}

CommandContext::~CommandContext()
{

}

void CommandContext::Reset()
{
	assert(m_pCommandList);
	if (m_pAllocator == nullptr)
	{
		m_pAllocator = m_pGraphics->GetCommandQueue(m_Type)->RequestAllocator();
		m_pCommandList->Reset(m_pAllocator, nullptr);
	}
	m_NumQueuedBarriers = 0;
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

	return fenceValue;
}

uint64 CommandContext::ExecuteAndReset(bool wait)
{
	FlushResourceBarriers();
	CommandQueue* pQueue = m_pGraphics->GetCommandQueue(m_Type);
	uint64 fenceValue = pQueue->ExecuteCommandList(m_pCommandList);

	if (wait)
	{
		pQueue->WaitForFence(fenceValue);
	}

	m_DynamicAllocator->Free(fenceValue);
	m_pCommandList->Reset(m_pAllocator, nullptr);

	return fenceValue;
}

void CommandContext::InsertResourceBarrier(GraphicsResource* pBuffer, D3D12_RESOURCE_STATES state, bool executeImmediate /*= false*/)
{
	if (state != pBuffer->m_CurrentState)
	{
		if (m_Type == D3D12_COMMAND_LIST_TYPE_COMPUTE)
		{
			D3D12_RESOURCE_STATES currentState = pBuffer->GetResourceState();
			assert((currentState & VALID_COMPUTE_QUEUE_RESOURCE_STATES) == currentState);
			assert((state & VALID_COMPUTE_QUEUE_RESOURCE_STATES) == state);
		}
		else if (m_Type == D3D12_COMMAND_LIST_TYPE_COPY)
		{
			D3D12_RESOURCE_STATES currentState = pBuffer->GetResourceState();
			assert((currentState & VALID_COPY_QUEUE_RESOURCE_STATES) == currentState);
			assert((state & VALID_COPY_QUEUE_RESOURCE_STATES) == state);
		}

		m_QueuedBarriers[m_NumQueuedBarriers] = CD3DX12_RESOURCE_BARRIER::Transition(pBuffer->GetResource(), pBuffer->m_CurrentState, state);
		++m_NumQueuedBarriers;
		if (executeImmediate || m_NumQueuedBarriers >= m_QueuedBarriers.size())
		{
			FlushResourceBarriers();
		}
		pBuffer->m_CurrentState = state;
	}
}

void CommandContext::InsertUavBarrier(GraphicsResource* pBuffer /*= nullptr*/, bool executeImmediate /*= false*/)
{
	m_QueuedBarriers[m_NumQueuedBarriers] = CD3DX12_RESOURCE_BARRIER::UAV(pBuffer ? pBuffer->GetResource() : nullptr);
	++m_NumQueuedBarriers;
	if (executeImmediate || m_NumQueuedBarriers >= m_QueuedBarriers.size())
	{
		FlushResourceBarriers();
	}
	if (pBuffer)
	{
		pBuffer->m_CurrentState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	}
}

void CommandContext::FlushResourceBarriers()
{
	if (m_NumQueuedBarriers > 0)
	{
		m_pCommandList->ResourceBarrier(m_NumQueuedBarriers, m_QueuedBarriers.data());
		m_NumQueuedBarriers = 0;
	}
}

void CommandContext::CopyResource(GraphicsBuffer* pSource, GraphicsBuffer* pTarget)
{
	assert(pSource);
	assert(pTarget);
	D3D12_RESOURCE_STATES sourceState = pSource->GetResourceState();
	D3D12_RESOURCE_STATES targetState = pTarget->GetResourceState();
	InsertResourceBarrier(pSource, D3D12_RESOURCE_STATE_COPY_SOURCE);
	InsertResourceBarrier(pTarget, D3D12_RESOURCE_STATE_COPY_DEST, true);
	m_pCommandList->CopyResource(pTarget->GetResource(), pSource->GetResource());
	InsertResourceBarrier(pSource, sourceState);
	InsertResourceBarrier(pTarget, targetState);
}

void CommandContext::InitializeBuffer(GraphicsBuffer* pResource, const void* pData, uint64 dataSize, uint64 offset)
{
	DynamicAllocation allocation = m_DynamicAllocator->Allocate(dataSize);
	memcpy(allocation.pMappedMemory, pData, dataSize);
	D3D12_RESOURCE_STATES previousState = pResource->GetResourceState();
	InsertResourceBarrier(pResource, D3D12_RESOURCE_STATE_COPY_DEST, true);
	m_pCommandList->CopyBufferRegion(pResource->GetResource(), offset, allocation.pBackingResource->GetResource(), allocation.Offset, dataSize);
	InsertResourceBarrier(pResource, previousState, true);
}

void CommandContext::InitializeTexture(Texture* pResource, D3D12_SUBRESOURCE_DATA* pSubResourceDatas, int firstSubResource, int subResourceCount)
{
	D3D12_RESOURCE_DESC desc = pResource->GetResource()->GetDesc();
	size_t requiredSize = 0;
	m_pGraphics->GetDevice()->GetCopyableFootprints(&desc, firstSubResource, subResourceCount, 0, nullptr, nullptr, nullptr, &requiredSize);
	DynamicAllocation allocation = m_DynamicAllocator->Allocate(requiredSize, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
	D3D12_RESOURCE_STATES previousState = pResource->GetResourceState();
	InsertResourceBarrier(pResource, D3D12_RESOURCE_STATE_COPY_DEST, true);
	UpdateSubresources(m_pCommandList, pResource->GetResource(), allocation.pBackingResource->GetResource(), allocation.Offset, firstSubResource, subResourceCount, pSubResourceDatas);
	InsertResourceBarrier(pResource, previousState, true);
}

void CommandContext::SetName(const char* pName)
{
	SetD3DObjectName(m_pCommandList, pName);
}

#pragma endregion BASE

#pragma region COPY

CopyCommandContext::CopyCommandContext(Graphics* pGraphics, ID3D12GraphicsCommandList* pCommandList, ID3D12CommandAllocator* pAllocator)
	: CommandContext(pGraphics, pCommandList, pAllocator)
{
	m_Type = D3D12_COMMAND_LIST_TYPE_COPY;
}

#pragma endregion

#pragma region COMPUTE

ComputeCommandContext::ComputeCommandContext(Graphics* pGraphics, ID3D12GraphicsCommandList* pCommandList, ID3D12CommandAllocator* pAllocator)
	: CopyCommandContext(pGraphics, pCommandList, pAllocator)
{
	m_CurrentContext = CommandListContext::Compute;
	m_Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
	m_pShaderResourceDescriptorAllocator = std::make_unique<DynamicDescriptorAllocator>(pGraphics, this, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	m_pSamplerDescriptorAllocator = std::make_unique<DynamicDescriptorAllocator>(pGraphics, this, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
}

void ComputeCommandContext::Dispatch(uint32 groupCountX, uint32 groupCountY, uint32 groupCountZ)
{
	assert(m_CurrentContext == CommandListContext::Compute);
	FlushResourceBarriers();
	m_pShaderResourceDescriptorAllocator->UploadAndBindStagedDescriptors(DescriptorTableType::Compute);
	m_pSamplerDescriptorAllocator->UploadAndBindStagedDescriptors(DescriptorTableType::Compute);
	m_pCommandList->Dispatch(groupCountX, groupCountY, groupCountZ);
}

void ComputeCommandContext::ExecuteIndirect(ID3D12CommandSignature* pCommandSignature, GraphicsBuffer* pIndirectArguments)
{
	assert(m_CurrentContext == CommandListContext::Compute);
	FlushResourceBarriers();
	m_pShaderResourceDescriptorAllocator->UploadAndBindStagedDescriptors(DescriptorTableType::Compute);
	m_pSamplerDescriptorAllocator->UploadAndBindStagedDescriptors(DescriptorTableType::Compute);
	m_pCommandList->ExecuteIndirect(pCommandSignature, 1, pIndirectArguments->GetResource(), 0, nullptr, 0);
}

void ComputeCommandContext::Reset()
{
	CommandContext::Reset();
	BindDescriptorHeaps();
}

uint64 ComputeCommandContext::Execute(bool wait)
{
	uint64 fenceValue = CommandContext::Execute(wait);
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

uint64 ComputeCommandContext::ExecuteAndReset(bool wait)
{
	uint64 fenceValue = CommandContext::ExecuteAndReset(wait);
	m_CurrentDescriptorHeaps = {};
	return fenceValue;
}

void ComputeCommandContext::ClearUavUInt(GraphicsBuffer* pBuffer, uint32 values[4])
{
	DescriptorHandle gpuHandle = m_pShaderResourceDescriptorAllocator->AllocateTransientDescriptor(1);
	m_pGraphics->GetDevice()->CopyDescriptorsSimple(1, gpuHandle.GetCpuHandle(), pBuffer->GetUAV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	m_pCommandList->ClearUnorderedAccessViewUint(gpuHandle.GetGpuHandle(), pBuffer->GetUAV(), pBuffer->GetResource(), values, 0, nullptr);
}

void ComputeCommandContext::ClearUavFloat(GraphicsBuffer* pBuffer, float values[4])
{
	DescriptorHandle gpuHandle = m_pShaderResourceDescriptorAllocator->AllocateTransientDescriptor(1);
	m_pGraphics->GetDevice()->CopyDescriptorsSimple(1, gpuHandle.GetCpuHandle(), pBuffer->GetUAV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	m_pCommandList->ClearUnorderedAccessViewFloat(gpuHandle.GetGpuHandle(), pBuffer->GetUAV(), pBuffer->GetResource(), values, 0, nullptr);
}

void ComputeCommandContext::SetComputePipelineState(ComputePipelineState* pPipelineState)
{
	m_pCommandList->SetPipelineState(pPipelineState->GetPipelineState());
	if (m_CurrentContext != CommandListContext::Compute)
	{
		Reset();
		m_CurrentContext = CommandListContext::Compute;
	}
}

void ComputeCommandContext::SetComputeRootSignature(RootSignature* pRootSignature)
{
	assert(m_CurrentContext == CommandListContext::Compute);
	m_pCommandList->SetComputeRootSignature(pRootSignature->GetRootSignature());
	m_pShaderResourceDescriptorAllocator->ParseRootSignature(pRootSignature);
	m_pSamplerDescriptorAllocator->ParseRootSignature(pRootSignature);
}

void ComputeCommandContext::SetComputeRootConstants(int rootIndex, uint32 count, const void* pConstants)
{
	assert(m_CurrentContext == CommandListContext::Compute);
	m_pCommandList->SetComputeRoot32BitConstants(rootIndex, count, pConstants, 0);
}

void ComputeCommandContext::SetComputeDynamicConstantBufferView(int rootIndex, void* pData, uint32 dataSize)
{
	assert(m_CurrentContext == CommandListContext::Compute);
	DynamicAllocation allocation = m_DynamicAllocator->Allocate(dataSize);
	memcpy(allocation.pMappedMemory, pData, dataSize);
	m_pCommandList->SetComputeRootConstantBufferView(rootIndex, allocation.GpuHandle);
}

void ComputeCommandContext::SetDynamicDescriptor(int rootIndex, int offset, D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
	m_pShaderResourceDescriptorAllocator->SetDescriptors(rootIndex, offset, 1, &handle);
}

void ComputeCommandContext::SetDynamicDescriptors(int rootIndex, int offset, D3D12_CPU_DESCRIPTOR_HANDLE* handles, int count)
{
	m_pShaderResourceDescriptorAllocator->SetDescriptors(rootIndex, offset, count, handles);
}

void ComputeCommandContext::SetDynamicSampler(int rootIndex, int offset, D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
	m_pSamplerDescriptorAllocator->SetDescriptors(rootIndex, offset, 1, &handle);
}

void ComputeCommandContext::SetDynamicSamplers(int rootIndex, int offset, D3D12_CPU_DESCRIPTOR_HANDLE* handles, int count)
{
	m_pSamplerDescriptorAllocator->SetDescriptors(rootIndex, offset, count, handles);
}

void ComputeCommandContext::SetDescriptorHeap(ID3D12DescriptorHeap* pHeap, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
	if (m_CurrentDescriptorHeaps[(int)type] != pHeap)
	{
		m_CurrentDescriptorHeaps[(int)type] = pHeap;
		BindDescriptorHeaps();
	}
}

void ComputeCommandContext::BindDescriptorHeaps()
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

#pragma endregion

#pragma region GRAPHICS

GraphicsCommandContext::GraphicsCommandContext(Graphics* pGraphics, ID3D12GraphicsCommandList* pCommandList, ID3D12CommandAllocator* pAllocator)
	: ComputeCommandContext(pGraphics, pCommandList, pAllocator)
{
	m_CurrentContext = CommandListContext::Graphics;
	m_Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
}

void GraphicsCommandContext::BeginRenderPass(const RenderPassInfo& renderPassInfo)
{
#if USE_RENDERPASSES
	ComPtr<ID3D12GraphicsCommandList4> pCmd;
	if (m_pCommandList->QueryInterface(IID_PPV_ARGS(pCmd.GetAddressOf())) == S_OK)
	{
		auto getRenderPassAccessBegin = [](RenderPassAccess access) {
			RenderTargetLoadAction loadAction = (RenderTargetLoadAction)((uint8)access >> 2);
			switch (loadAction)
			{
			case RenderTargetLoadAction::DontCare: return D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_DISCARD;
			case RenderTargetLoadAction::Load: return D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE;
			case RenderTargetLoadAction::Clear: return D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
			}
			return D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_NO_ACCESS;
		};

		auto getRenderPassAccessEnd = [](RenderPassAccess access) {
			RenderTargetStoreAction storeAction = (RenderTargetStoreAction)((uint8)access & 0b11);
			switch (storeAction)
			{
			case RenderTargetStoreAction::DontCare: return D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_DISCARD;
			case RenderTargetStoreAction::Store: return D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;
			case RenderTargetStoreAction::Resolve: return D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_RESOLVE;
			}
			return D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_NO_ACCESS;
		};

		D3D12_RENDER_PASS_BEGINNING_ACCESS depthAccessBegin{ getRenderPassAccessBegin(renderPassInfo.DepthStencilTarget.Access) };
		if (depthAccessBegin.Type == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR)
		{
			assert(renderPassInfo.DepthStencilTarget.Texture->GetClearBinding().BindingValue == ClearBinding::ClearBindingValue::DepthStencil);
			depthAccessBegin.Clear.ClearValue.DepthStencil.Depth = renderPassInfo.DepthStencilTarget.Texture->GetClearBinding().DepthStencil.Depth;
			depthAccessBegin.Clear.ClearValue.Format = renderPassInfo.DepthStencilTarget.Texture->GetFormat();
		}
		D3D12_RENDER_PASS_ENDING_ACCESS depthAccessEnd{ getRenderPassAccessEnd(renderPassInfo.DepthStencilTarget.Access) };

		bool writeable = true;
		if (depthAccessEnd.Type == D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_DISCARD)
		{
			writeable = false;
		}

		D3D12_RENDER_PASS_BEGINNING_ACCESS stencilAccessBegin{ getRenderPassAccessBegin(renderPassInfo.DepthStencilTarget.StencilAccess) };
		if (stencilAccessBegin.Type == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR)
		{
			assert(renderPassInfo.DepthStencilTarget.Texture->GetClearBinding().BindingValue == ClearBinding::ClearBindingValue::DepthStencil);
			stencilAccessBegin.Clear.ClearValue.DepthStencil.Stencil = renderPassInfo.DepthStencilTarget.Texture->GetClearBinding().DepthStencil.Stencil;
			stencilAccessBegin.Clear.ClearValue.Format = renderPassInfo.DepthStencilTarget.Texture->GetFormat();
		}
		D3D12_RENDER_PASS_ENDING_ACCESS stencilAccessEnd{ getRenderPassAccessEnd(renderPassInfo.DepthStencilTarget.StencilAccess) };

		D3D12_RENDER_PASS_DEPTH_STENCIL_DESC renderPassDepthStencilDesc{ renderPassInfo.DepthStencilTarget.Texture->GetDSV(writeable), depthAccessBegin, stencilAccessBegin, depthAccessEnd, stencilAccessEnd };

		std::array<D3D12_RENDER_PASS_RENDER_TARGET_DESC, 4> renderTargetDescs;
		for (uint32 i = 0; i < renderPassInfo.RenderTargetCount; ++i)
		{
			const RenderPassInfo::RenderTargetInfo& data = renderPassInfo.RenderTargets[i];
			D3D12_RENDER_PASS_BEGINNING_ACCESS renderTargetAccessBegin{ getRenderPassAccessBegin(data.Access) };
			if (renderTargetAccessBegin.Type == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR)
			{
				assert(data.Texture->GetClearBinding().BindingValue == ClearBinding::ClearBindingValue::Color);
				memcpy(renderTargetAccessBegin.Clear.ClearValue.Color, &data.Texture->GetClearBinding().Color.x, sizeof(Color));
				renderTargetAccessBegin.Clear.ClearValue.Format = data.Texture->GetFormat();
			}
			D3D12_RENDER_PASS_ENDING_ACCESS renderTargetAccessEnd{ getRenderPassAccessEnd(data.Access) };
			uint32 subResource = D3D12CalcSubresource(data.MipLevel, data.ArrayIndex, 0, data.Texture->GetMipLevels(), data.Texture->GetArraySize());
			renderTargetDescs[i] = D3D12_RENDER_PASS_RENDER_TARGET_DESC{ data.Texture->GetRTV(subResource), renderTargetAccessBegin, renderTargetAccessEnd };
		}
		
		pCmd->BeginRenderPass(renderPassInfo.RenderTargetCount, renderTargetDescs.data(), &renderPassDepthStencilDesc, D3D12_RENDER_PASS_FLAG_NONE);	
	}
	else
#endif
	{
		bool writeable = true;
		if ((RenderTargetLoadAction)((uint8)renderPassInfo.DepthStencilTarget.Access >> 2) == RenderTargetLoadAction::DontCare)
		{
			writeable = false;
		}

		D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = renderPassInfo.DepthStencilTarget.Texture->GetDSV(writeable);
		D3D12_CLEAR_FLAGS clearFlags = (D3D12_CLEAR_FLAGS)0;
		if ((RenderTargetLoadAction)((uint8)renderPassInfo.DepthStencilTarget.Access >> 2) == RenderTargetLoadAction::Clear)
		{
			clearFlags |= D3D12_CLEAR_FLAGS::D3D12_CLEAR_FLAG_DEPTH;
		}
		if ((RenderTargetLoadAction)((uint8)renderPassInfo.DepthStencilTarget.Access >> 2) == RenderTargetLoadAction::Clear)
		{
			clearFlags |= D3D12_CLEAR_FLAGS::D3D12_CLEAR_FLAG_STENCIL;
		}
		if (clearFlags != (D3D12_CLEAR_FLAGS)0)
		{
			const ClearBinding& clearBinding = renderPassInfo.DepthStencilTarget.Texture->GetClearBinding();
			assert(clearBinding.BindingValue == ClearBinding::ClearBindingValue::DepthStencil);
			m_pCommandList->ClearDepthStencilView(dsvHandle, clearFlags, clearBinding.DepthStencil.Depth, clearBinding.DepthStencil.Stencil, 0, nullptr);
		}

		std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 4> rtvs;
		for (uint32 i = 0; i < renderPassInfo.RenderTargetCount; ++i)
		{
			const RenderPassInfo::RenderTargetInfo& data = renderPassInfo.RenderTargets[i];
			uint32 subResource = D3D12CalcSubresource(data.MipLevel, data.ArrayIndex, 0, data.Texture->GetMipLevels(), data.Texture->GetArraySize());
			rtvs[i] = data.Texture->GetRTV(subResource);
			
			if ((RenderTargetLoadAction)((uint8)data.Access >> 2) == RenderTargetLoadAction::Clear)
			{
				assert(data.Texture->GetClearBinding().BindingValue == ClearBinding::ClearBindingValue::Color);
				m_pCommandList->ClearRenderTargetView(data.Texture->GetRTV(subResource), &data.Texture->GetClearBinding().Color.x, 0, nullptr);
			}
		}
		m_pCommandList->OMSetRenderTargets(renderPassInfo.RenderTargetCount, rtvs.data(), false, &dsvHandle);
	}
}

void GraphicsCommandContext::EndRenderPass()
{
#if USE_RENDERPASSES
	ComPtr<ID3D12GraphicsCommandList4> pCmd;

	if (m_pCommandList->QueryInterface(IID_PPV_ARGS(pCmd.GetAddressOf())) == S_OK)
	{
		pCmd->EndRenderPass();
	}
#endif
}

void GraphicsCommandContext::Draw(int vertexStart, int vertexCount)
{
	FlushResourceBarriers();
	m_pShaderResourceDescriptorAllocator->UploadAndBindStagedDescriptors(DescriptorTableType::Graphics);
	m_pSamplerDescriptorAllocator->UploadAndBindStagedDescriptors(DescriptorTableType::Graphics);
	m_pCommandList->DrawInstanced(vertexCount, 1, vertexStart, 0);
}

void GraphicsCommandContext::DrawIndexed(int indexCount, int indexStart, int minVertex /*= 0*/)
{
	FlushResourceBarriers();
	m_pShaderResourceDescriptorAllocator->UploadAndBindStagedDescriptors(DescriptorTableType::Graphics);
	m_pSamplerDescriptorAllocator->UploadAndBindStagedDescriptors(DescriptorTableType::Graphics);
	m_pCommandList->DrawIndexedInstanced(indexCount, 1, indexStart, minVertex, 0);
}

void GraphicsCommandContext::DrawIndexedInstanced(int indexCount, int indexStart, int instanceCount, int minVertex /*= 0*/, int instanceStart /*= 0*/)
{
	FlushResourceBarriers();
	m_pShaderResourceDescriptorAllocator->UploadAndBindStagedDescriptors(DescriptorTableType::Graphics);
	m_pSamplerDescriptorAllocator->UploadAndBindStagedDescriptors(DescriptorTableType::Graphics);
	m_pCommandList->DrawIndexedInstanced(indexCount, instanceCount, indexStart, minVertex, instanceStart);
}

void GraphicsCommandContext::ClearRenderTarget(D3D12_CPU_DESCRIPTOR_HANDLE rtv, const Color& color /*= Color(0.15f, 0.15f, 0.15f, 1.0f)*/)
{
	m_pCommandList->ClearRenderTargetView(rtv, &color.x, 0, nullptr);
}

void GraphicsCommandContext::ClearDepth(D3D12_CPU_DESCRIPTOR_HANDLE dsv, D3D12_CLEAR_FLAGS clearFlags /*= D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL*/, float depth /*= 1.0f*/, unsigned char stencil /*= 0*/)
{
	m_pCommandList->ClearDepthStencilView(dsv, clearFlags, depth, stencil, 0, nullptr);
}

void GraphicsCommandContext::SetGraphicsPipelineState(GraphicsPipelineState* pPipelineState)
{
	m_pCommandList->SetPipelineState(pPipelineState->GetPipelineState());
	if (m_CurrentContext != CommandListContext::Graphics)
	{
		Reset();
		m_CurrentContext = CommandListContext::Graphics;
	}
}

void GraphicsCommandContext::SetGraphicsRootSignature(RootSignature* pRootSignature)
{
	assert(m_CurrentContext == CommandListContext::Graphics);
	m_pCommandList->SetGraphicsRootSignature(pRootSignature->GetRootSignature());
	m_pShaderResourceDescriptorAllocator->ParseRootSignature(pRootSignature);
	m_pSamplerDescriptorAllocator->ParseRootSignature(pRootSignature);
}

void GraphicsCommandContext::SetGraphicsRootConstants(int rootIndex, uint32 count, const void* pConstants)
{
	assert(m_CurrentContext == CommandListContext::Graphics);
	m_pCommandList->SetGraphicsRoot32BitConstants(rootIndex, count, pConstants, 0);
}

void GraphicsCommandContext::SetDynamicConstantBufferView(int rootIndex, void* pData, uint32 dataSize)
{
	DynamicAllocation allocation = m_DynamicAllocator->Allocate(dataSize);
	memcpy(allocation.pMappedMemory, pData, dataSize);
	m_pCommandList->SetGraphicsRootConstantBufferView(rootIndex, allocation.GpuHandle);
}

void GraphicsCommandContext::SetDynamicVertexBuffer(int rootIndex, int elementCount, int elementSize, void* pData)
{
	assert(m_CurrentContext == CommandListContext::Graphics);
	int bufferSize = elementCount * elementSize;
	DynamicAllocation allocation = m_DynamicAllocator->Allocate(bufferSize);
	memcpy(allocation.pMappedMemory, pData, bufferSize);
	D3D12_VERTEX_BUFFER_VIEW view = {};
	view.BufferLocation = allocation.GpuHandle;
	view.SizeInBytes = bufferSize;
	view.StrideInBytes = elementSize;
	m_pCommandList->IASetVertexBuffers(rootIndex, 1, &view);
}

void GraphicsCommandContext::SetDynamicIndexBuffer(int elementCount, void* pData, bool smallIndices /*= false*/)
{
	assert(m_CurrentContext == CommandListContext::Graphics);
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

void GraphicsCommandContext::SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY type)
{
	m_pCommandList->IASetPrimitiveTopology(type);
}

void GraphicsCommandContext::SetVertexBuffer(VertexBuffer* pVertexBuffer)
{
	SetVertexBuffers(pVertexBuffer, 1);
}

void GraphicsCommandContext::SetVertexBuffers(VertexBuffer* pVertexBuffers, int bufferCount)
{
	assert(bufferCount <= 4);
	std::array<D3D12_VERTEX_BUFFER_VIEW, 4> views = {};
	for (int i = 0; i < bufferCount; ++i)
	{
		views[i] = pVertexBuffers->GetView();
	}
	m_pCommandList->IASetVertexBuffers(0, bufferCount, views.data());
}

void GraphicsCommandContext::SetIndexBuffer(IndexBuffer* pIndexBuffer)
{
	const D3D12_INDEX_BUFFER_VIEW& view = pIndexBuffer->GetView();
	m_pCommandList->IASetIndexBuffer(&view);
}

void GraphicsCommandContext::SetViewport(const FloatRect& rect, float minDepth /*= 0.0f*/, float maxDepth /*= 1.0f*/)
{
	D3D12_VIEWPORT viewport;
	viewport.TopLeftX = (float)rect.Left;
	viewport.TopLeftY = (float)rect.Top;
	viewport.Height = (float)rect.GetHeight();
	viewport.Width = (float)rect.GetWidth();
	viewport.MinDepth = minDepth;
	viewport.MaxDepth = maxDepth;
	m_pCommandList->RSSetViewports(1, &viewport);
}

void GraphicsCommandContext::SetScissorRect(const FloatRect& rect)
{
	D3D12_RECT r;
	r.left = (LONG)rect.Left;
	r.top = (LONG)rect.Top;
	r.right = (LONG)rect.Right;
	r.bottom = (LONG)rect.Bottom;
	m_pCommandList->RSSetScissorRects(1, &r);
}

#pragma endregion GRAPHICS
