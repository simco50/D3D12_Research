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
	checkf(m_RootDescriptorMask.GetBit(rootIndex), "RootSignature does not have a DescriptorTable at root index %d", rootIndex);
	check(numHandles + offset <= m_RootDescriptorTable[rootIndex].TableSize);

	RootDescriptorEntry& entry = m_RootDescriptorTable[rootIndex];
	bool dirty = false;
	for (uint32 i = 0; i < numHandles; ++i)
	{
		if (entry.TableStart[i + offset].ptr != pHandles[i].ptr)
		{
			entry.TableStart[i + offset] = pHandles[i];
			entry.AssignedHandlesBitMap.SetBit(i + offset);
			dirty = true;
		}
	}
	if (dirty)
	{
		m_StaleRootParameters.SetBit(rootIndex);
	}
}

void OnlineDescriptorAllocator::UploadAndBindStagedDescriptors(DescriptorTableType descriptorTableType)
{
	if (m_StaleRootParameters.HasAnyBitSet() == false)
	{
		return;
	}

	for (auto it = m_StaleRootParameters.GetSetBitsIterator(); it.Valid(); ++it)
	{
		uint32 rootIndex = it.Value();
		RootDescriptorEntry& entry = m_RootDescriptorTable[rootIndex];

		uint32 rangeSize = 0;
		if (entry.AssignedHandlesBitMap.MostSignificantBit(&rangeSize))
		{
			rangeSize++; //Size = highest bit + 1
			DescriptorHandle gpuHandle = Allocate(rangeSize);
			DescriptorHandle currentOffset = gpuHandle;
			for (uint32 descriptorIndex = 0; descriptorIndex < entry.TableSize; ++descriptorIndex)
			{
				if (entry.AssignedHandlesBitMap.GetBit(descriptorIndex))
				{
					GetParent()->GetDevice()->CopyDescriptorsSimple(1, currentOffset.GetCpuHandle(), entry.TableStart[descriptorIndex], m_Type);
				}
				currentOffset += m_pHeapAllocator->GetDescriptorSize();
			}

			switch (descriptorTableType)
			{
			case DescriptorTableType::Graphics:
				m_pOwner->GetCommandList()->SetGraphicsRootDescriptorTable(rootIndex, gpuHandle.GetGpuHandle());
				break;
			case DescriptorTableType::Compute:
				m_pOwner->GetCommandList()->SetComputeRootDescriptorTable(rootIndex, gpuHandle.GetGpuHandle());
				break;
			default:
				noEntry();
				break;
			}
		}
	}

	m_StaleRootParameters.ClearAll();
}

void OnlineDescriptorAllocator::EnsureSpace(uint32 count)
{
	if (!m_pCurrentHeapBlock || m_pCurrentHeapBlock->Size - m_pCurrentHeapBlock->CurrentOffset < count)
	{
		if (m_pCurrentHeapBlock)
		{
			m_ReleasedBlocks.push_back(m_pCurrentHeapBlock);
		}
		m_pCurrentHeapBlock = m_pHeapAllocator->AllocateBlock();
	}
}

void OnlineDescriptorAllocator::ParseRootSignature(RootSignature* pRootSignature)
{
	m_RootDescriptorMask = m_Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER ?
		pRootSignature->GetSamplerTableMask() : pRootSignature->GetDescriptorTableMask();

	m_StaleRootParameters.ClearAll();
	memset(m_HandleCache.data(), 0, m_HandleCache.size() * sizeof(D3D12_CPU_DESCRIPTOR_HANDLE));

	uint32 offset = 0;
	for (auto it = m_RootDescriptorMask.GetSetBitsIterator(); it.Valid(); ++it)
	{
		int rootIndex = it.Value();
		RootDescriptorEntry& entry = m_RootDescriptorTable[rootIndex];
		entry.AssignedHandlesBitMap.ClearAll();
		uint32 tableSize = pRootSignature->GetDescriptorTableSizes()[rootIndex];
		checkf(tableSize <= MAX_DESCRIPTORS_PER_TABLE, "The descriptor table at root index %d is too large. Size is %d, maximum is %d.", rootIndex, tableSize, MAX_DESCRIPTORS_PER_TABLE);
		check(tableSize > 0);
		entry.TableSize = tableSize;
		entry.TableStart = &m_HandleCache[offset];
		offset += entry.TableSize;
		checkf(offset <= m_HandleCache.size(), "Out of DescriptorTable handles!");
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

void OnlineDescriptorAllocator::UnbindAll()
{
	m_StaleRootParameters.ClearAll();
	for (auto it = m_RootDescriptorMask.GetSetBitsIterator(); it.Valid(); ++it)
	{
		uint32 rootIndex = it.Value();
		if (m_RootDescriptorTable[rootIndex].AssignedHandlesBitMap.HasAnyBitSet())
		{
			m_StaleRootParameters.SetBit(rootIndex);
		}
	}
}

DescriptorHandle OnlineDescriptorAllocator::Allocate(int descriptorCount)
{
	EnsureSpace(descriptorCount);
	DescriptorHandle handle = m_pCurrentHeapBlock->StartHandle + m_pCurrentHeapBlock->CurrentOffset * m_pHeapAllocator->GetDescriptorSize();
	m_pCurrentHeapBlock->CurrentOffset += descriptorCount;
	return handle;
}
