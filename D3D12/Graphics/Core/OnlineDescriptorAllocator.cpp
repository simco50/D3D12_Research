#include "stdafx.h"
#include "OnlineDescriptorAllocator.h"
#include "Graphics.h"
#include "RootSignature.h"
#include "CommandContext.h"
#include "CommandQueue.h"

GlobalOnlineDescriptorHeap::GlobalOnlineDescriptorHeap(GraphicsDevice* pParent, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32 blockSize, uint32 numDescriptors)
	: GraphicsObject(pParent), m_Type(type), m_NumDescriptors(numDescriptors), m_BlockSize(blockSize)
{
	checkf(numDescriptors % blockSize == 0, "Number of descriptors must be a multiple of blockSize (%d)", blockSize);
	checkf(type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV || type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, "Online Descriptor Heap must be either of CBV/SRV/UAV or Sampler type.");

	D3D12_DESCRIPTOR_HEAP_DESC desc{};
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	desc.NodeMask = 0;
	desc.NumDescriptors = numDescriptors;
	desc.Type = type;
	pParent->GetDevice()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(m_pHeap.GetAddressOf()));
	D3D::SetObjectName(m_pHeap.Get(), type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ? "Global CBV/SRV/UAV Heap" : "Global Sampler Heap");

	m_DescriptorSize = pParent->GetDevice()->GetDescriptorHandleIncrementSize(type);
	m_StartHandle = DescriptorHandle(m_pHeap->GetCPUDescriptorHandleForHeapStart(), 0, m_pHeap->GetGPUDescriptorHandleForHeapStart());

	uint32 numBlocks = m_NumDescriptors / blockSize;

	DescriptorHandle currentOffset = m_StartHandle;
	for (uint32 i = 0; i < numBlocks; ++i)
	{
		m_HeapBlocks.emplace_back(std::make_unique<DescriptorHeapBlock>(currentOffset, blockSize));
		m_FreeBlocks.push(m_HeapBlocks.back().get());
		currentOffset.OffsetInline(blockSize, m_DescriptorSize);
	}
}

DescriptorHeapBlock* GlobalOnlineDescriptorHeap::AllocateBlock()
{
	std::lock_guard<std::mutex> lock(m_BlockAllocateMutex);

	// Check if we can free so finished blocks
	for (uint32 i = 0; i < (uint32)m_ReleasedBlocks.size(); ++i)
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

PersistentDescriptorAllocator::PersistentDescriptorAllocator(GlobalOnlineDescriptorHeap* pGlobalHeap)
	: GraphicsObject(pGlobalHeap->GetParent()), m_pHeapAllocator(pGlobalHeap)
{

}

DescriptorHandle PersistentDescriptorAllocator::Allocate()
{
	std::lock_guard<std::mutex> lock(m_AllocationLock);

	if (m_NumAllocated >= m_FreeHandles.size())
	{
		m_HeapBlocks.push_back(m_pHeapAllocator->AllocateBlock());
		m_FreeHandles.resize(m_FreeHandles.size() + m_HeapBlocks.back()->Size);
		int i = m_NumAllocated;
		auto generate = [&i]() { return i++; };
		std::generate(m_FreeHandles.begin() + m_NumAllocated, m_FreeHandles.end(), generate);
	}

	uint32 index = m_FreeHandles[m_NumAllocated];
	++m_NumAllocated;
	uint32 blockIndex = index / m_pHeapAllocator->GetBlockSize();
	check(blockIndex < m_HeapBlocks.size());
	DescriptorHandle handle = m_HeapBlocks[blockIndex]->StartHandle;
	uint32 elementIndex = index % m_pHeapAllocator->GetBlockSize();
	check(elementIndex < m_HeapBlocks[blockIndex]->Size);
	handle.OffsetInline(elementIndex, m_pHeapAllocator->GetDescriptorSize());
	return handle;
}

void PersistentDescriptorAllocator::Free(DescriptorHandle& handle)
{
	check(!handle.IsNull());
	Free(handle.HeapIndex);
	handle.Reset();
}

void PersistentDescriptorAllocator::Free(int32& heapIndex)
{
	assert(heapIndex >= 0);
	std::lock_guard<std::mutex> lock(m_AllocationLock);
	check(m_HeapBlocks.size() > 0);
	uint32 index = heapIndex - m_HeapBlocks[0]->StartHandle.HeapIndex;
	check(index >= 0);

	while (m_DeletionQueue.size())
	{
		const auto& f = m_DeletionQueue.front();
		if (GetParent()->GetFrameFence()->IsComplete(f.second))
		{
			--m_NumAllocated;
			m_FreeHandles[m_NumAllocated] = f.first;
			m_DeletionQueue.pop();
		}
		else
		{
			break;
		}
	}

	m_DeletionQueue.emplace(index, GetParent()->GetFrameFence()->GetCurrentValue());
	heapIndex = DescriptorHandle::InvalidHeapIndex;
}

OnlineDescriptorAllocator::OnlineDescriptorAllocator(GlobalOnlineDescriptorHeap* pGlobalHeap)
	: GraphicsObject(pGlobalHeap->GetParent()), m_Type(pGlobalHeap->GetType()), m_pHeapAllocator(pGlobalHeap)
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
		checkf(tableSize != ~0u, "Descriptor table at RootIndex '%d' is unbounded and should not use the descriptor allocator", rootIndex);
		checkf(offset + numHandles <= tableSize, "Attempted to set a descriptor (Offset: %d, Range: %d) out of the descriptor table bounds (Size: %d)", offset, numHandles, rootIndex);
		
		entry.Descriptor = Allocate(tableSize);
		m_StaleRootParameters.SetBit(rootIndex);
	}

	DescriptorHandle targetHandle = entry.Descriptor.Offset(offset, m_pHeapAllocator->GetDescriptorSize());
	for (uint32 i = 0; i < numHandles; ++i)
	{
		checkf(pHandles[i].ptr != DescriptorHandle::InvalidCPUHandle.ptr, "Invalid Descriptor provided (RootIndex: %d, Offset: %d)", rootIndex, offset + i);
		GetParent()->GetDevice()->CopyDescriptorsSimple(1, targetHandle.CpuHandle, pHandles[i], m_Type);
		targetHandle.OffsetInline(1, m_pHeapAllocator->GetDescriptorSize());
	}
}

void OnlineDescriptorAllocator::BindStagedDescriptors(ID3D12GraphicsCommandList* pCmdList, CommandListContext descriptorTableType)
{
	for (uint32 rootIndex : m_StaleRootParameters)
	{
		if (m_StaleRootParameters.GetBit(rootIndex))
		{
			RootDescriptorEntry& entry = m_RootDescriptorTable[rootIndex];
			switch (descriptorTableType)
			{
			case CommandListContext::Graphics:
				pCmdList->SetGraphicsRootDescriptorTable(rootIndex, entry.Descriptor.GpuHandle);
				break;
			case CommandListContext::Compute:
				pCmdList->SetComputeRootDescriptorTable(rootIndex, entry.Descriptor.GpuHandle);
				break;
			default:
				noEntry();
				break;
			}
		}
	}

	m_StaleRootParameters.ClearAll();
}

void OnlineDescriptorAllocator::ParseRootSignature(RootSignature* pRootSignature)
{
	m_RootDescriptorMask = m_Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER ?
		pRootSignature->GetSamplerTableMask() : pRootSignature->GetDescriptorTableMask();

	m_StaleRootParameters.ClearAll();

	const std::array<uint32, MAX_NUM_ROOT_PARAMETERS>& descriptorTableSizes = pRootSignature->GetDescriptorTableSizes();
	for (uint32 rootIndex : m_RootDescriptorMask)
	{
		RootDescriptorEntry& entry = m_RootDescriptorTable[rootIndex];
		entry.TableSize = descriptorTableSizes[rootIndex];
		entry.Descriptor.Reset();
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

	DescriptorHandle handle = m_pCurrentHeapBlock->StartHandle.Offset(m_pCurrentHeapBlock->CurrentOffset, m_pHeapAllocator->GetDescriptorSize());
	m_pCurrentHeapBlock->CurrentOffset += descriptorCount;
	return handle;
}
