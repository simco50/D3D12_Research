#include "stdafx.h"
#include "GPUDescriptorHeap.h"
#include "Graphics.h"
#include "RootSignature.h"
#include "CommandContext.h"
#include "CommandQueue.h"

GPUDescriptorHeap::GPUDescriptorHeap(GraphicsDevice* pParent, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32 dynamicPageSize, uint32 numDescriptors)
	: DeviceObject(pParent), m_Type(type), m_DynamicPageSize(dynamicPageSize), m_NumDynamicDescriptors(numDescriptors / 2), m_NumPersistentDescriptors(numDescriptors / 2), m_PersistentHandles(numDescriptors / 2)
{
	check(dynamicPageSize >= 32, "Page size must be at least 128 (is %d)", dynamicPageSize);
	check(m_NumDynamicDescriptors % dynamicPageSize == 0, "Number of descriptors must be a multiple of Page Size (%d)", dynamicPageSize);
	check(type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV || type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, "Online Descriptor Heap must be either of CBV/SRV/UAV or Sampler type.");

	D3D12_DESCRIPTOR_HEAP_DESC desc{};
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	desc.NodeMask = 0;
	desc.NumDescriptors = numDescriptors;
	desc.Type = type;
	pParent->GetDevice()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(m_pHeap.GetAddressOf()));
	D3D::SetObjectName(m_pHeap.Get(), type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ? "GPU CBV/SRV/UAV Descriptor Heap" : "GPU Sampler Descriptor Heap");

	m_DescriptorSize = pParent->GetDevice()->GetDescriptorHandleIncrementSize(type);
	m_StartHandle = DescriptorHandle(m_pHeap->GetCPUDescriptorHandleForHeapStart(), 0, m_pHeap->GetGPUDescriptorHandleForHeapStart());

	uint32 numPages = m_NumDynamicDescriptors / dynamicPageSize;

	DescriptorHandle currentOffset = m_StartHandle.Offset(m_NumPersistentDescriptors, m_DescriptorSize);
	for (uint32 i = 0; i < numPages; ++i)
	{
		m_DynamicPages.emplace_back(std::make_unique<DescriptorHeapPage>(currentOffset, dynamicPageSize));
		m_FreeDynamicPages.push_back(m_DynamicPages.back().get());
		currentOffset.OffsetInline(dynamicPageSize, m_DescriptorSize);
	}
}

GPUDescriptorHeap::~GPUDescriptorHeap()
{
	CleanupPersistent();

	CleanupDynamic();
	check(m_ReleasedDynamicPages.size() == 0, "Not all dynamic GPU descriptors are freed.");
	check(m_FreeDynamicPages.size() == m_DynamicPages.size(), "Not all dynamic GPU descriptor pages are freed.");
}

DescriptorHandle GPUDescriptorHeap::AllocatePersistent()
{
	std::lock_guard lock(m_AllocationLock);
	if (!m_PersistentHandles.CanAllocate())
	{
		CleanupPersistent();
	}

	check(m_PersistentHandles.CanAllocate(), "Out of persistent descriptor heap space (%d), increase heap size", m_NumPersistentDescriptors);
	return m_StartHandle.Offset(m_PersistentHandles.Allocate(), m_DescriptorSize);
}

void GPUDescriptorHeap::FreePersistent(uint32& heapIndex)
{
	check(heapIndex != DescriptorHandle::InvalidHeapIndex);
	std::lock_guard lock(m_AllocationLock);
	m_PersistentDeletionQueue.emplace(heapIndex, GetParent()->GetFrameFence()->GetCurrentValue());
	heapIndex = DescriptorHandle::InvalidHeapIndex;
}

DescriptorHeapPage* GPUDescriptorHeap::AllocateDynamicPage()
{
	std::lock_guard lock(m_DynamicPageAllocateMutex);
	if (m_FreeDynamicPages.empty())
	{
		CleanupDynamic();
	}

	check(!m_FreeDynamicPages.empty(), "Ran out of dynamic descriptor heap space (%d). Increase heap size.", m_NumDynamicDescriptors);
	DescriptorHeapPage* pPage = m_FreeDynamicPages.back();
	m_FreeDynamicPages.pop_back();
	return pPage;
}

void GPUDescriptorHeap::FreeDynamicPage(const SyncPoint& syncPoint, DescriptorHeapPage* pPage)
{
	std::lock_guard lock(m_DynamicPageAllocateMutex);
	pPage->SyncPoint = syncPoint;
	pPage->CurrentOffset = 0;
	m_ReleasedDynamicPages.push(pPage);
}

void GPUDescriptorHeap::CleanupDynamic()
{
	while (!m_ReleasedDynamicPages.empty())
	{
		DescriptorHeapPage* pPage = m_ReleasedDynamicPages.front();
		if (!pPage->SyncPoint.IsComplete())
			break;

		m_ReleasedDynamicPages.pop();
		m_FreeDynamicPages.push_back(pPage);
	}
}

void GPUDescriptorHeap::CleanupPersistent()
{
	while (m_PersistentDeletionQueue.size())
	{
		const auto& f = m_PersistentDeletionQueue.front();
		if (!GetParent()->GetFrameFence()->IsComplete(f.second))
			break;

		m_PersistentHandles.Free(f.first);
		m_PersistentDeletionQueue.pop();
	}
}

DynamicGPUDescriptorAllocator::DynamicGPUDescriptorAllocator(GPUDescriptorHeap* pGlobalHeap)
	: DeviceObject(pGlobalHeap->GetParent()), m_Type(pGlobalHeap->GetType()), m_pHeapAllocator(pGlobalHeap)
{
}

DynamicGPUDescriptorAllocator::~DynamicGPUDescriptorAllocator()
{
	if (m_pCurrentHeapPage)
	{
		m_ReleasedPages.push_back(m_pCurrentHeapPage);
	}
	Fence* pFrameFence = GetParent()->GetFrameFence();
	SyncPoint syncPoint(pFrameFence, pFrameFence->GetLastSignaledValue());
	ReleaseUsedHeaps(syncPoint);
}

void DynamicGPUDescriptorAllocator::SetDescriptors(uint32 rootIndex, uint32 offset, Span<const ResourceView*> handles)
{
	m_StaleRootParameters.SetBit(rootIndex);

	StagedDescriptorTable& table = m_StagedDescriptors[rootIndex];
	check(table.Capacity != 0, "Root parameter at index '%d' is not a descriptor table", rootIndex);
	check(offset + handles.GetSize() <= table.Capacity, "Descriptor table at root index '%d' is too small (is %d but requires %d)", rootIndex, table.Capacity, offset + handles.GetSize());

	table.Descriptors.resize(Math::Max((uint32)table.Descriptors.size(), offset + handles.GetSize()));
	table.StartIndex = Math::Min(offset, table.StartIndex);
	for (int i = 0; i < (int)handles.GetSize(); ++i)
	{
		check(handles[i]);
		table.Descriptors[offset + i] = handles[i]->GetDescriptor();
	}
}

void DynamicGPUDescriptorAllocator::BindStagedDescriptors(CommandContext& context, CommandListContext descriptorTableType)
{
	for (uint32 rootIndex : m_StaleRootParameters)
	{
		StagedDescriptorTable& table = m_StagedDescriptors[rootIndex];
		DescriptorHandle handle = Allocate((uint32)table.Descriptors.size());
		for (int i = table.StartIndex; i < table.Descriptors.size(); ++i)
		{
			if (table.Descriptors[i].ptr != DescriptorHandle::InvalidCPUHandle.ptr && table.Descriptors[i].ptr != 0)
			{
				DescriptorHandle target = handle.Offset(i, m_pHeapAllocator->GetDescriptorSize());
				GetParent()->GetDevice()->CopyDescriptorsSimple(1, target.CpuHandle, table.Descriptors[i], m_Type);
			}
		}
		table.Descriptors.clear();
		table.StartIndex = 0xFFFFFFFF;

		if (descriptorTableType == CommandListContext::Graphics)
			context.GetCommandList()->SetGraphicsRootDescriptorTable(rootIndex, handle.GpuHandle);
		else if (descriptorTableType == CommandListContext::Compute)
			context.GetCommandList()->SetComputeRootDescriptorTable(rootIndex, handle.GpuHandle);
		else
			noEntry();
	}
	m_StaleRootParameters.ClearAll();
}

void DynamicGPUDescriptorAllocator::ParseRootSignature(const RootSignature* pRootSignature)
{
	for (uint32 i = 0; i < (uint32)m_StagedDescriptors.size(); ++i)
	{
		StagedDescriptorTable& table = m_StagedDescriptors[i];
		table.StartIndex = 0xFFFFFFF;

		if (i < pRootSignature->GetNumRootParameters())
			table.Capacity = pRootSignature->GetDescriptorTableSize(i);
		else
			table.Capacity = 0;
	}
	m_StaleRootParameters.ClearAll();
}

void DynamicGPUDescriptorAllocator::ReleaseUsedHeaps(const SyncPoint& syncPoint)
{
	for (DescriptorHeapPage* pPage : m_ReleasedPages)
		m_pHeapAllocator->FreeDynamicPage(syncPoint, pPage);
	m_ReleasedPages.clear();
}

DescriptorHandle DynamicGPUDescriptorAllocator::Allocate(uint32 descriptorCount)
{
	if (!m_pCurrentHeapPage || m_pCurrentHeapPage->Size - m_pCurrentHeapPage->CurrentOffset < descriptorCount)
	{
		if (m_pCurrentHeapPage)
			m_ReleasedPages.push_back(m_pCurrentHeapPage);
		m_pCurrentHeapPage = m_pHeapAllocator->AllocateDynamicPage();
	}

	DescriptorHandle handle = m_pCurrentHeapPage->StartHandle.Offset(m_pCurrentHeapPage->CurrentOffset, m_pHeapAllocator->GetDescriptorSize());
	m_pCurrentHeapPage->CurrentOffset += descriptorCount;
	return handle;
}
