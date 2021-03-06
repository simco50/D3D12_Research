#include "stdafx.h"
#include "OnlineDescriptorAllocator.h"
#include "Graphics.h"
#include "RootSignature.h"
#include "CommandContext.h"

GlobalOnlineDescriptorHeap::GlobalOnlineDescriptorHeap(Graphics* pParent, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32 blockSize, uint32 numDescriptors)
	: GraphicsObject(pParent), m_Type(type), m_NumDescriptors(numDescriptors)
{
	checkf(numDescriptors % blockSize == 0, "Number of descriptors must be a multiple of blockSize (%d)", blockSize);

	D3D12_DESCRIPTOR_HEAP_DESC desc{};
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	desc.NodeMask = 0;
	desc.NumDescriptors = numDescriptors;
	desc.Type = type;
	pParent->GetDevice()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(m_pHeap.GetAddressOf()));
	D3D::SetObjectName(m_pHeap.Get(), "Global Online Descriptor Heap");

	m_DescriptorSize = pParent->GetDevice()->GetDescriptorHandleIncrementSize(type);
	m_StartHandle = DescriptorHandle(m_pHeap->GetCPUDescriptorHandleForHeapStart(), m_pHeap->GetGPUDescriptorHandleForHeapStart());

	uint32 numBlocks = m_NumDescriptors / blockSize;

	DescriptorHandle currentOffset = m_StartHandle;
	for (uint32 i = 0; i < numBlocks; ++i)
	{
		m_HeapBlocks.emplace_back(std::make_unique<DescriptorHeapBlock>(currentOffset, blockSize, 0));
		m_FreeBlocks.push(m_HeapBlocks.back().get());
		currentOffset += blockSize * m_DescriptorSize;
	}
}

DescriptorHeapBlock* GlobalOnlineDescriptorHeap::AllocateBlock()
{
	std::lock_guard<std::mutex> lock(m_BlockAllocateMutex);

	// Check if we can free so finished blocks
	for (int i = 0; i < m_ReleasedBlocks.size(); ++i)
	{
		DescriptorHeapBlock* pBlock = m_ReleasedBlocks[i];
		if (GetParent()->IsFenceComplete(pBlock->FenceValue))
		{
			std::swap(m_ReleasedBlocks[i], m_ReleasedBlocks.back());
			m_ReleasedBlocks.pop_back();
			m_FreeBlocks.push(pBlock);
			--i;
		}
	}

	checkf(!m_FreeBlocks.empty(), "Ran out of descriptor heap space. Must increase the number of descriptors.");

	DescriptorHeapBlock* pBlock = m_FreeBlocks.front();
	m_FreeBlocks.pop();
	return pBlock;
}

void GlobalOnlineDescriptorHeap::FreeBlock(uint64 fenceValue, DescriptorHeapBlock* pBlock)
{
	std::lock_guard<std::mutex> lock(m_BlockAllocateMutex);
	pBlock->FenceValue = fenceValue;
	pBlock->CurrentOffset = 0;
	m_ReleasedBlocks.push_back(pBlock);
}

OnlineDescriptorAllocator::OnlineDescriptorAllocator(GlobalOnlineDescriptorHeap* pGlobalHeap, CommandContext* pContext)
	: GraphicsObject(pGlobalHeap->GetParent()), m_pOwner(pContext), m_Type(pGlobalHeap->GetType()), m_pHeapAllocator(pGlobalHeap)
{
}

OnlineDescriptorAllocator::~OnlineDescriptorAllocator()
{

}

void OnlineDescriptorAllocator::SetDescriptors(uint32 rootIndex, uint32 offset, uint32 numHandles, const D3D12_CPU_DESCRIPTOR_HANDLE* pHandles)
{
	RootDescriptorEntry& entry = m_RootDescriptorTable[rootIndex];
	if (!m_StaleRootParameters.GetBit(rootIndex))
	{
		uint32 tableSize = entry.TableSize;
		entry.GpuHandle = Allocate(tableSize);
		m_StaleRootParameters.SetBit(rootIndex);
	}

	DescriptorHandle targetHandle = entry.GpuHandle;
	targetHandle += offset * m_pHeapAllocator->GetDescriptorSize();
	for (uint32 i = 0; i < numHandles; ++i)
	{
		checkf(pHandles[i].ptr != DescriptorHandle::InvalidCPUHandle.ptr, "Invalid Descriptor provided (RootIndex: %d, Offset: %d)", rootIndex, offset + i);
		GetParent()->GetDevice()->CopyDescriptorsSimple(1, targetHandle.GetCpuHandle(), pHandles[i], m_Type);
		targetHandle += m_pHeapAllocator->GetDescriptorSize();
	}
}

void OnlineDescriptorAllocator::BindStagedDescriptors(GraphicsPipelineType descriptorTableType)
{
	if (m_StaleRootParameters.HasAnyBitSet() == false)
	{
		return;
	}

	for (uint32 rootIndex : m_StaleRootParameters)
	{
		RootDescriptorEntry& entry = m_RootDescriptorTable[rootIndex];
		switch (descriptorTableType)
		{
		case GraphicsPipelineType::Graphics:
			m_pOwner->GetCommandList()->SetGraphicsRootDescriptorTable(rootIndex, entry.GpuHandle.GetGpuHandle());
			break;
		case GraphicsPipelineType::Compute:
			m_pOwner->GetCommandList()->SetComputeRootDescriptorTable(rootIndex, entry.GpuHandle.GetGpuHandle());
			break;
		default:
			noEntry();
			break;
		}
	}

	m_StaleRootParameters.ClearAll();
}

void OnlineDescriptorAllocator::ParseRootSignature(RootSignature* pRootSignature)
{
	m_RootDescriptorMask = m_Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER ?
		pRootSignature->GetSamplerTableMask() : pRootSignature->GetDescriptorTableMask();

	m_StaleRootParameters.ClearAll();

	for (uint32 rootIndex : m_RootDescriptorMask)
	{
		RootDescriptorEntry& entry = m_RootDescriptorTable[rootIndex];
		entry.TableSize = pRootSignature->GetDescriptorTableSizes()[rootIndex];
	}
}

void OnlineDescriptorAllocator::ReleaseUsedHeaps(uint64 fenceValue)
{
	for (DescriptorHeapBlock* pBlock : m_ReleasedBlocks)
	{
		m_pHeapAllocator->FreeBlock(fenceValue, pBlock);
	}
	m_ReleasedBlocks.clear();
}

DescriptorHandle OnlineDescriptorAllocator::Allocate(uint32 descriptorCount)
{
	if (!m_pCurrentHeapBlock || m_pCurrentHeapBlock->Size - m_pCurrentHeapBlock->CurrentOffset < descriptorCount)
	{
		if (m_pCurrentHeapBlock)
		{
			m_ReleasedBlocks.push_back(m_pCurrentHeapBlock);
		}
		m_pCurrentHeapBlock = m_pHeapAllocator->AllocateBlock();
	}

	DescriptorHandle handle = m_pCurrentHeapBlock->StartHandle + m_pCurrentHeapBlock->CurrentOffset * m_pHeapAllocator->GetDescriptorSize();
	m_pCurrentHeapBlock->CurrentOffset += descriptorCount;
	return handle;
}
