#include "stdafx.h"
#include "GPUDescriptorHeap.h"
#include "Graphics.h"
#include "RootSignature.h"
#include "CommandContext.h"
#include "CommandQueue.h"

GPUDescriptorHeap::GPUDescriptorHeap(GraphicsDevice* pParent, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32 dynamicBlockSize, uint32 numDescriptors)
	: GraphicsObject(pParent), m_Type(type), m_DynamicBlockSize(dynamicBlockSize), m_NumDynamicDescriptors(numDescriptors / 2), m_NumPersistentDescriptors(numDescriptors / 2), m_PersistentHandles(numDescriptors / 2, false)
{
	checkf(dynamicBlockSize >= 32, "Block size must be at least 128 (is %d)", dynamicBlockSize);
	checkf(m_NumDynamicDescriptors % dynamicBlockSize == 0, "Number of descriptors must be a multiple of blockSize (%d)", dynamicBlockSize);
	checkf(type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV || type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, "Online Descriptor Heap must be either of CBV/SRV/UAV or Sampler type.");

	D3D12_DESCRIPTOR_HEAP_DESC desc{};
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	desc.NodeMask = 0;
	desc.NumDescriptors = numDescriptors;
	desc.Type = type;
	pParent->GetDevice()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(m_pHeap.GetAddressOf()));
	D3D::SetObjectName(m_pHeap.Get(), type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ? "GPU CBV/SRV/UAV Heap" : "GPU Sampler Heap");

	m_DescriptorSize = pParent->GetDevice()->GetDescriptorHandleIncrementSize(type);
	m_StartHandle = DescriptorHandle(m_pHeap->GetCPUDescriptorHandleForHeapStart(), 0, m_pHeap->GetGPUDescriptorHandleForHeapStart());

	uint32 numBlocks = m_NumDynamicDescriptors / dynamicBlockSize;

	DescriptorHandle currentOffset = m_StartHandle.Offset(m_NumPersistentDescriptors, m_DescriptorSize);
	for (uint32 i = 0; i < numBlocks; ++i)
	{
		m_DynamicBlocks.emplace_back(std::make_unique<DescriptorHeapBlock>(currentOffset, dynamicBlockSize));
		m_FreeDynamicBlocks.push_back(m_DynamicBlocks.back().get());
		currentOffset.OffsetInline(dynamicBlockSize, m_DescriptorSize);
	}
}

GPUDescriptorHeap::~GPUDescriptorHeap()
{
	CleanupPersistent();
	checkf(m_PersistentHandles.GetNumAllocations() == 0, "Not all persistent GPU descriptors are freed.");

	CleanupDynamic();
	checkf(m_ReleasedDynamicBlocks.size() == 0, "Not all dynamic GPU descriptors are freed.");
}

DescriptorHandle GPUDescriptorHeap::AllocatePersistent()
{
	std::lock_guard lock(m_AllocationLock);

	if (!m_PersistentHandles.CanAllocate())
	{
		CleanupPersistent();
	}

	checkf(m_PersistentHandles.CanAllocate(), "Out of persistent descriptor heap space (%d), increase heap size", m_NumPersistentDescriptors);
	return m_StartHandle.Offset(m_PersistentHandles.Allocate(), m_DescriptorSize);
}

void GPUDescriptorHeap::FreePersistent(uint32& heapIndex)
{
	check(heapIndex != DescriptorHandle::InvalidHeapIndex);
	std::lock_guard lock(m_AllocationLock);
	m_PersistentDeletionQueue.emplace(heapIndex, GetParent()->GetFrameFence()->GetCurrentValue());
	heapIndex = DescriptorHandle::InvalidHeapIndex;
}

DescriptorHeapBlock* GPUDescriptorHeap::AllocateDynamicBlock()
{
	std::lock_guard lock(m_DynamicBlockAllocateMutex);

	if (m_FreeDynamicBlocks.empty())
	{
		CleanupDynamic();
	}

	checkf(!m_FreeDynamicBlocks.empty(), "Ran out of dynamic descriptor heap space (%d). Increase heap size.", m_NumDynamicDescriptors);

	DescriptorHeapBlock* pBlock = m_FreeDynamicBlocks.back();
	m_FreeDynamicBlocks.pop_back();
	return pBlock;
}

void GPUDescriptorHeap::FreeDynamicBlock(const SyncPoint& syncPoint, DescriptorHeapBlock* pBlock)
{
	std::lock_guard lock(m_DynamicBlockAllocateMutex);
	pBlock->SyncPoint = syncPoint;
	pBlock->CurrentOffset = 0;
	m_ReleasedDynamicBlocks.push(pBlock);
}

void GPUDescriptorHeap::CleanupDynamic()
{
	while (!m_ReleasedDynamicBlocks.empty())
	{
		DescriptorHeapBlock* pBlock = m_ReleasedDynamicBlocks.front();
		if (!pBlock->SyncPoint.IsComplete())
		{
			break;
		}
		m_ReleasedDynamicBlocks.pop();
		m_FreeDynamicBlocks.push_back(pBlock);
	}
}

void GPUDescriptorHeap::CleanupPersistent()
{
	while (m_PersistentDeletionQueue.size())
	{
		const auto& f = m_PersistentDeletionQueue.front();
		if (GetParent()->GetFrameFence()->IsComplete(f.second))
		{
			m_PersistentHandles.Free(f.first);
			m_PersistentDeletionQueue.pop();
		}
		else
		{
			break;
		}
	}
}

DynamicGPUDescriptorAllocator::DynamicGPUDescriptorAllocator(GPUDescriptorHeap* pGlobalHeap)
	: GraphicsObject(pGlobalHeap->GetParent()), m_Type(pGlobalHeap->GetType()), m_pHeapAllocator(pGlobalHeap)
{
}

void DynamicGPUDescriptorAllocator::SetDescriptors(uint32 rootIndex, uint32 offset, const Span< D3D12_CPU_DESCRIPTOR_HANDLE>& handles)
{
	RootDescriptorEntry& entry = m_RootDescriptorTable[rootIndex];
	if (!m_StaleRootParameters.GetBit(rootIndex))
	{
		uint32 tableSize = entry.TableSize;
		checkf(tableSize != ~0u, "Descriptor table at RootIndex '%d' is unbounded and should not use the descriptor allocator", rootIndex);
		checkf(offset + handles.GetSize() <= tableSize, "Attempted to set a descriptor (Offset: %d, Range: %d) out of the descriptor table bounds (Size: %d)", offset, handles.GetSize(), rootIndex);
		
		entry.Descriptor = Allocate(tableSize);
		m_StaleRootParameters.SetBit(rootIndex);
	}

	DescriptorHandle targetHandle = entry.Descriptor.Offset(offset, m_pHeapAllocator->GetDescriptorSize());
	for(uint32 i = 0; i < handles.GetSize(); ++i)
	{
		checkf(handles[i].ptr != DescriptorHandle::InvalidCPUHandle.ptr, "Invalid Descriptor provided (RootIndex: %d, Offset: %d)", rootIndex, offset + i);
		GetParent()->GetDevice()->CopyDescriptorsSimple(1, targetHandle.CpuHandle, handles[i], m_Type);
		targetHandle.OffsetInline(1, m_pHeapAllocator->GetDescriptorSize());
	}
}

void DynamicGPUDescriptorAllocator::BindStagedDescriptors(CommandContext& context, CommandListContext descriptorTableType)
{
	for (uint32 rootIndex : m_StaleRootParameters)
	{
		RootDescriptorEntry& entry = m_RootDescriptorTable[rootIndex];
		switch (descriptorTableType)
		{
		case CommandListContext::Graphics:
			context.GetCommandList()->SetGraphicsRootDescriptorTable(rootIndex, entry.Descriptor.GpuHandle);
			break;
		case CommandListContext::Compute:
			context.GetCommandList()->SetComputeRootDescriptorTable(rootIndex, entry.Descriptor.GpuHandle);
			break;
		default:
			noEntry();
			break;
		}
	}

	m_StaleRootParameters.ClearAll();
}

void DynamicGPUDescriptorAllocator::ParseRootSignature(const RootSignature* pRootSignature)
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

void DynamicGPUDescriptorAllocator::ReleaseUsedHeaps(const SyncPoint& syncPoint)
{
	for (DescriptorHeapBlock* pBlock : m_ReleasedBlocks)
	{
		m_pHeapAllocator->FreeDynamicBlock(syncPoint, pBlock);
	}
	m_ReleasedBlocks.clear();
}

DescriptorHandle DynamicGPUDescriptorAllocator::Allocate(uint32 descriptorCount)
{
	if (!m_pCurrentHeapBlock || m_pCurrentHeapBlock->Size - m_pCurrentHeapBlock->CurrentOffset < descriptorCount)
	{
		if (m_pCurrentHeapBlock)
		{
			m_ReleasedBlocks.push_back(m_pCurrentHeapBlock);
		}
		m_pCurrentHeapBlock = m_pHeapAllocator->AllocateDynamicBlock();
	}

	DescriptorHandle handle = m_pCurrentHeapBlock->StartHandle.Offset(m_pCurrentHeapBlock->CurrentOffset, m_pHeapAllocator->GetDescriptorSize());
	m_pCurrentHeapBlock->CurrentOffset += descriptorCount;
	return handle;
}
