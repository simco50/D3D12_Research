#include "stdafx.h"
#include "OnlineDescriptorAllocator.h"
#include "Graphics.h"
#include "RootSignature.h"
#include "CommandContext.h"

std::vector<ComPtr<ID3D12DescriptorHeap>> OnlineDescriptorAllocator::m_DescriptorHeaps;
std::array<std::queue<std::pair<uint64, ID3D12DescriptorHeap*>>, 2> OnlineDescriptorAllocator::m_FreeDescriptors;
std::mutex OnlineDescriptorAllocator::m_HeapAllocationMutex;

OnlineDescriptorAllocator::OnlineDescriptorAllocator(Graphics* pGraphics, CommandContext* pContext, D3D12_DESCRIPTOR_HEAP_TYPE type)
	: GraphicsObject(pGraphics), m_pOwner(pContext), m_Type(type)
{
	m_DescriptorSize = m_pGraphics->GetDevice()->GetDescriptorHandleIncrementSize(type);
}

OnlineDescriptorAllocator::~OnlineDescriptorAllocator()
{

}

DescriptorHandle OnlineDescriptorAllocator::AllocateTransientDescriptor(int count)
{
	if (HasSpace(count) == false)
	{
		ReleaseHeap();
		UnbindAll();
	}
	m_pOwner->SetDescriptorHeap(GetHeap(), m_Type);
	return Allocate(count);
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
	if (m_StaleRootParameters.AnyBitSet() == false)
	{
		return;
	}

	uint32 requiredSpace = GetRequiredSpace();
	if (HasSpace(requiredSpace) == false)
	{
		ReleaseHeap();
		UnbindAll();
		requiredSpace = GetRequiredSpace();
	}
	m_pOwner->SetDescriptorHeap(GetHeap(), m_Type);

	DescriptorHandle gpuHandle = Allocate(requiredSpace);

	uint32 sourceRangeCount = 0;
	uint32 destinationRangeCount = 0;
	std::array<D3D12_CPU_DESCRIPTOR_HANDLE, MAX_DESCRIPTORS_PER_COPY> sourceRanges = {};
	std::array<D3D12_CPU_DESCRIPTOR_HANDLE, MAX_DESCRIPTORS_PER_COPY> destinationRanges = {};
	std::array<uint32, MAX_DESCRIPTORS_PER_COPY> sourceRangeSizes = {};
	std::array<uint32, MAX_DESCRIPTORS_PER_COPY> destinationRangeSizes = {};

	int tableCount = 0;
	std::array<D3D12_GPU_DESCRIPTOR_HANDLE, MAX_NUM_ROOT_PARAMETERS> newDescriptorTables = {};

	for (auto it = m_StaleRootParameters.GetSetBitsIterator(); it.Valid(); ++it)
	{
		//If the rangecount exceeds the max amount of descriptors per copy, flush
		if (sourceRangeCount >= MAX_DESCRIPTORS_PER_COPY)
		{
			m_pGraphics->GetDevice()->CopyDescriptors(destinationRangeCount, destinationRanges.data(), destinationRangeSizes.data(), sourceRangeCount, sourceRanges.data(), sourceRangeSizes.data(), m_Type);
			sourceRangeCount = 0;
			destinationRangeCount = 0;
		}

		uint32 rootIndex = it.Value();
		RootDescriptorEntry& entry = m_RootDescriptorTable[rootIndex];

		uint32 rangeSize = 0;
		entry.AssignedHandlesBitMap.MostSignificantBit(&rangeSize);
		rangeSize += 1;

		//Copy the descriptors one by one because they aren't necessarily memory contiguous
		for (auto it = entry.AssignedHandlesBitMap.GetSetBitsIterator(); it.Valid(); ++it)
		{
			uint32 i = it.Value();
			sourceRangeSizes[sourceRangeCount] = 1;
			check(entry.TableStart[i].ptr);
			sourceRanges[sourceRangeCount] = entry.TableStart[i];
			++sourceRangeCount;
		}

		destinationRanges[destinationRangeCount] = gpuHandle.GetCpuHandle();
		destinationRangeSizes[destinationRangeCount] = rangeSize;
		++destinationRangeCount;

		newDescriptorTables[tableCount++] = gpuHandle.GetGpuHandle();

		gpuHandle += rangeSize * m_DescriptorSize;
	}

	m_pGraphics->GetDevice()->CopyDescriptors(destinationRangeCount, destinationRanges.data(), destinationRangeSizes.data(), sourceRangeCount, sourceRanges.data(), sourceRangeSizes.data(), m_Type);

	int i = 0;
	for (auto it = m_StaleRootParameters.GetSetBitsIterator(); it.Valid(); ++it)
	{
		uint32 rootIndex = it.Value();
		switch (descriptorTableType)
		{
		case DescriptorTableType::Graphics:
			m_pOwner->GetCommandList()->SetGraphicsRootDescriptorTable(rootIndex, newDescriptorTables[i++]);
			break;
		case DescriptorTableType::Compute:
			m_pOwner->GetCommandList()->SetComputeRootDescriptorTable(rootIndex, newDescriptorTables[i++]);
			break;
		default:
			noEntry();
			break;
		}
	}

	m_StaleRootParameters.ClearAll();
}

bool OnlineDescriptorAllocator::HasSpace(int count)
{
	return m_pCurrentHeap && m_CurrentOffset + count <= DESCRIPTORS_PER_HEAP;
}

ID3D12DescriptorHeap* OnlineDescriptorAllocator::GetHeap()
{
	if (m_pCurrentHeap == nullptr)
	{
		m_pCurrentHeap = RequestNewHeap(m_Type);
		m_StartHandle = DescriptorHandle(m_pCurrentHeap->GetCPUDescriptorHandleForHeapStart(), m_pCurrentHeap->GetGPUDescriptorHandleForHeapStart());
	}
	return m_pCurrentHeap;
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
		check(tableSize > 0);
		entry.TableSize = tableSize;
		entry.TableStart = &m_HandleCache[offset];
		offset += entry.TableSize;
		checkf(offset <= DESCRIPTORS_PER_HEAP, "Out of DescriptorTable handles!");
	}
}

void OnlineDescriptorAllocator::ReleaseUsedHeaps(uint64 fenceValue)
{
	ReleaseHeap();
	{
		std::scoped_lock<std::mutex> lock(m_HeapAllocationMutex);
		for (ID3D12DescriptorHeap* pHeap : m_UsedDescriptorHeaps)
		{
			m_FreeDescriptors[(int)m_Type].emplace(fenceValue, pHeap);
		}
	}
	m_UsedDescriptorHeaps.clear();
}

uint32 OnlineDescriptorAllocator::GetRequiredSpace()
{
	uint32 requiredSpace = 0;
	for (auto it = m_StaleRootParameters.GetSetBitsIterator(); it.Valid(); ++it)
	{
		uint32 rootIndex = it.Value();
		uint32 maxHandle = 0;
		m_RootDescriptorTable[rootIndex].AssignedHandlesBitMap.MostSignificantBit(&maxHandle);
		requiredSpace += (uint32)maxHandle + 1;
	}

	return requiredSpace;
}

ID3D12DescriptorHeap* OnlineDescriptorAllocator::RequestNewHeap(D3D12_DESCRIPTOR_HEAP_TYPE type)
{
	std::scoped_lock<std::mutex> lock(m_HeapAllocationMutex);
	if (m_FreeDescriptors[(int)m_Type].size() > 0 && m_pGraphics->IsFenceComplete(m_FreeDescriptors[(int)m_Type].front().first))
	{
		ID3D12DescriptorHeap* pHeap = m_FreeDescriptors[(int)m_Type].front().second;
		m_FreeDescriptors[(int)m_Type].pop();
		return pHeap;
	}
	else
	{
		ComPtr<ID3D12DescriptorHeap> pHeap;
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		desc.NumDescriptors = DESCRIPTORS_PER_HEAP;
		desc.NodeMask = 0;
		desc.Type = type;
		VERIFY_HR_EX(m_pGraphics->GetDevice()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(pHeap.GetAddressOf())), m_pGraphics->GetDevice());
		D3D::SetObjectName(pHeap.Get(), "Online Pooled Descriptor Heap");
		m_DescriptorHeaps.push_back(std::move(pHeap));
		return m_DescriptorHeaps.back().Get();
	}
}

void OnlineDescriptorAllocator::ReleaseHeap()
{
	if (m_CurrentOffset == 0)
	{
		check(m_pCurrentHeap == nullptr);
		return;
	}
	check(m_pCurrentHeap);
	m_UsedDescriptorHeaps.push_back(m_pCurrentHeap);
	m_pCurrentHeap = nullptr;
	m_CurrentOffset = 0;
}

void OnlineDescriptorAllocator::UnbindAll()
{
	m_StaleRootParameters.ClearAll();
	for (auto it = m_RootDescriptorMask.GetSetBitsIterator(); it.Valid(); ++it)
	{
		uint32 rootIndex = it.Value();
		if (m_RootDescriptorTable[rootIndex].AssignedHandlesBitMap.AnyBitSet())
		{
			m_StaleRootParameters.SetBit(rootIndex);
		}
	}
}

DescriptorHandle OnlineDescriptorAllocator::Allocate(int descriptorCount)
{
	DescriptorHandle handle = m_StartHandle + m_CurrentOffset * m_DescriptorSize;
	m_CurrentOffset += descriptorCount;
	return handle;
}