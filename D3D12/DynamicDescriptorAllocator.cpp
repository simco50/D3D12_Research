#include "stdafx.h"
#include "DynamicDescriptorAllocator.h"
#include "Graphics.h"
#include "RootSignature.h"
#include "CommandContext.h"

std::vector<ComPtr<ID3D12DescriptorHeap>> DynamicDescriptorAllocator::m_DescriptorHeaps;
std::queue<std::pair<uint64, ID3D12DescriptorHeap*>> DynamicDescriptorAllocator::m_FreeDescriptors;

DynamicDescriptorAllocator::DynamicDescriptorAllocator(Graphics* pGraphics, CommandContext* pContext, D3D12_DESCRIPTOR_HEAP_TYPE type)
	: m_pGraphics(pGraphics), m_pOwner(pContext), m_Type(type)
{
	m_DescriptorSize = m_pGraphics->GetDevice()->GetDescriptorHandleIncrementSize(type);
}

DynamicDescriptorAllocator::~DynamicDescriptorAllocator()
{

}

void DynamicDescriptorAllocator::SetDescriptors(uint32 rootIndex, uint32 offset, uint32 numHandles, const D3D12_CPU_DESCRIPTOR_HANDLE* pHandles)
{
	assert(m_RootDescriptorMask.GetBit(rootIndex));
	assert(numHandles + offset <= m_RootDescriptorTable[rootIndex].TableSize);

	RootDescriptorEntry& entry = m_RootDescriptorTable[rootIndex];
	for (uint32 i = 0; i < numHandles; ++i)
	{
		entry.TableStart[i + offset] = pHandles[i];
		entry.AssignedHandlesBitMap.SetBit(i + offset);
	}
	m_StaleRootParameters.SetBit(rootIndex);
}

void DynamicDescriptorAllocator::UploadAndBindStagedDescriptors()
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
	int descriptorOffset = 0;

	uint32 sourceRangeCount = 0;
	uint32 destinationRangeCount = 0;
	std::array<D3D12_CPU_DESCRIPTOR_HANDLE, MAX_DESCRIPTORS_PER_COPY> sourceRanges = {};
	std::array<D3D12_CPU_DESCRIPTOR_HANDLE, MAX_DESCRIPTORS_PER_COPY> destinationRanges = {};
	std::array<uint32, MAX_DESCRIPTORS_PER_COPY> sourceRangeSizes = {};
	std::array<uint32, MAX_DESCRIPTORS_PER_COPY> destinationRangeSizes = {};

	for (auto it = m_StaleRootParameters.GetSetBitsIterator(); it.Valid(); ++it)
	{
		if (sourceRangeCount >= MAX_DESCRIPTORS_PER_COPY)
		{
			m_pGraphics->GetDevice()->CopyDescriptors(destinationRangeCount, destinationRanges.data(), destinationRangeSizes.data(), sourceRangeCount, sourceRanges.data(), sourceRangeSizes.data(), m_Type);
			sourceRangeCount = 0;
			destinationRangeCount = 0;
		}

		int rootIndex = it.Value();
		RootDescriptorEntry& entry = m_RootDescriptorTable[rootIndex];

		uint32 rangeSize = 0;
		entry.AssignedHandlesBitMap.MostSignificantBit(&rangeSize);
		rangeSize += 1;

		//Copy the descriptors one by one because they aren't necessarily memory contiguous
		for (int i = 0; i < rangeSize; ++i)
		{
			sourceRangeSizes[sourceRangeCount] = 1;
			sourceRanges[sourceRangeCount] = entry.TableStart[i];
			++sourceRangeCount;
		}

		destinationRanges[destinationRangeCount] = gpuHandle.GetCpuHandle();
		destinationRangeSizes[destinationRangeCount] = rangeSize;
		++destinationRangeCount;

		m_pOwner->GetCommandList()->SetGraphicsRootDescriptorTable(rootIndex, gpuHandle.GetGpuHandle());

		gpuHandle += descriptorOffset * m_DescriptorSize;
		descriptorOffset += rangeSize;
	}

	m_StaleRootParameters.ClearAll();

	m_pGraphics->GetDevice()->CopyDescriptors(destinationRangeCount, destinationRanges.data(), destinationRangeSizes.data(), sourceRangeCount, sourceRanges.data(), sourceRangeSizes.data(), m_Type);
}

bool DynamicDescriptorAllocator::HasSpace(int count)
{
	return m_pCurrentHeap && m_CurrentOffset + count <= DESCRIPTORS_PER_HEAP;
}

ID3D12DescriptorHeap* DynamicDescriptorAllocator::GetHeap()
{
	if (m_pCurrentHeap == nullptr)
	{
		m_pCurrentHeap = RequestNewHeap(m_Type);
		m_StartHandle = DescriptorHandle(m_pCurrentHeap->GetCPUDescriptorHandleForHeapStart(), m_pCurrentHeap->GetGPUDescriptorHandleForHeapStart());
	}
	return m_pCurrentHeap;
}

void DynamicDescriptorAllocator::ParseRootSignature(RootSignature* pRootSignature)
{
	m_RootDescriptorMask = m_Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER ? 
		pRootSignature->GetSamplerTableMask() : pRootSignature->GetDescriptorTableMask();

	m_StaleRootParameters.ClearAll();

	uint32 offset = 0;
	for (auto it = m_RootDescriptorMask.GetSetBitsIterator(); it.Valid(); ++it)
	{
		int rootIndex = it.Value();
		RootDescriptorEntry& entry = m_RootDescriptorTable[rootIndex];
		entry.AssignedHandlesBitMap.ClearAll();
		uint32 tableSize = pRootSignature->GetDescriptorTableSizes()[rootIndex];
		assert(tableSize > 0);
		entry.TableSize = tableSize;
		entry.TableStart = &m_HandleCache[offset];
		offset += entry.TableSize;
	}
}

void DynamicDescriptorAllocator::ReleaseUsedHeaps(uint64 fenceValue)
{
	ReleaseHeap();
	for (ID3D12DescriptorHeap* pHeap : m_UsedDescriptorHeaps)
	{
		m_FreeDescriptors.emplace(fenceValue, pHeap);
	}
	m_UsedDescriptorHeaps.clear();
}

uint32 DynamicDescriptorAllocator::GetRequiredSpace()
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

ID3D12DescriptorHeap* DynamicDescriptorAllocator::RequestNewHeap(D3D12_DESCRIPTOR_HEAP_TYPE type)
{
	if (m_FreeDescriptors.size() > 0 && m_pGraphics->IsFenceComplete(m_FreeDescriptors.front().first))
	{
		ID3D12DescriptorHeap* pHeap = m_FreeDescriptors.front().second;
		m_FreeDescriptors.pop();
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
		HR(m_pGraphics->GetDevice()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(pHeap.GetAddressOf())));
		m_DescriptorHeaps.push_back(std::move(pHeap));
		return m_DescriptorHeaps.back().Get();
	}
}

void DynamicDescriptorAllocator::ReleaseHeap()
{
	if (m_CurrentOffset == 0)
	{
		assert(m_pCurrentHeap == nullptr);
		return;
	}
	assert(m_pCurrentHeap);
	m_UsedDescriptorHeaps.push_back(m_pCurrentHeap);
	m_pCurrentHeap = nullptr;
	m_CurrentOffset = 0;
}

void DynamicDescriptorAllocator::UnbindAll()
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

DescriptorHandle DynamicDescriptorAllocator::Allocate(int descriptorCount)
{
	DescriptorHandle handle = m_StartHandle + m_CurrentOffset * m_DescriptorSize;
	m_CurrentOffset += descriptorCount;
	return handle;
}