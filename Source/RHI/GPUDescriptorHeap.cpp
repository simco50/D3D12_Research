#include "stdafx.h"
#include "GPUDescriptorHeap.h"
#include "Device.h"
#include "RootSignature.h"
#include "CommandContext.h"
#include "CommandQueue.h"

GPUDescriptorHeap::GPUDescriptorHeap(GraphicsDevice* pParent, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32 scratchPageSize, uint32 numDescriptors)
	: DeviceObject(pParent), m_Type(type), m_ScratchPageSize(scratchPageSize), m_NumScratchDescriptors(numDescriptors / 2), m_NumPersistentDescriptors(numDescriptors / 2), m_PersistentHandles(numDescriptors / 2)
{
	gAssert(scratchPageSize >= 32, "Page size must be at least 128 (is %d)", scratchPageSize);
	gAssert(m_NumScratchDescriptors % scratchPageSize == 0, "Number of descriptors must be a multiple of Page Size (%d)", scratchPageSize);
	gAssert(type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV || type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, "Online Descriptor Heap must be either of CBV/SRV/UAV or Sampler type.");

	// Create a GPU visible descriptor heap
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc{};
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		desc.NodeMask = 0;
		desc.NumDescriptors = numDescriptors;
		desc.Type = type;
		pParent->GetDevice()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(m_pHeap.GetAddressOf()));
		D3D::SetObjectName(m_pHeap.Get(), type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ? "GPU CBV/SRV/UAV Descriptor Heap" : "GPU Sampler Descriptor Heap");
	}
	// Create a CPU opaque descriptor heap for all persistent descriptors
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc{};
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		desc.NodeMask = 0;
		desc.NumDescriptors = m_NumPersistentDescriptors;
		desc.Type = type;
		pParent->GetDevice()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(m_pCPUHeap.GetAddressOf()));
		D3D::SetObjectName(m_pCPUHeap.Get(), type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ? "GPU CBV/SRV/UAV CPU Opaque Descriptor Heap" : "GPU Sampler CPU Opaque Descriptor Heap");
	}

	m_DescriptorSize = pParent->GetDevice()->GetDescriptorHandleIncrementSize(type);
	m_StartPtr = DescriptorPtr
	{
		.CPUHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_pHeap->GetCPUDescriptorHandleForHeapStart()),
		.GPUHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(m_pHeap->GetGPUDescriptorHandleForHeapStart()),
		.CPUOpaqueHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_pCPUHeap->GetCPUDescriptorHandleForHeapStart()),
		.HeapIndex = 0
	};

	uint32 numPages = m_NumScratchDescriptors / scratchPageSize;

	DescriptorPtr currentOffset = m_StartPtr.Offset(m_NumPersistentDescriptors, m_DescriptorSize);
	for (uint32 i = 0; i < numPages; ++i)
	{
		m_ScratchPages.emplace_back(std::make_unique<DescriptorHeapPage>(currentOffset, scratchPageSize));
		m_FreeScratchPages.push_back(m_ScratchPages.back().get());
		currentOffset = currentOffset.Offset(scratchPageSize, m_DescriptorSize);
	}
}

GPUDescriptorHeap::~GPUDescriptorHeap()
{
	CleanupPersistent();

	CleanupScratch();
	gAssert(m_ReleasedScratchPages.size() == 0, "Not all scratch GPU descriptors are freed.");
	gAssert(m_FreeScratchPages.size() == m_ScratchPages.size(), "Not all scratch GPU descriptor pages are freed.");
}

DescriptorPtr GPUDescriptorHeap::AllocatePersistent()
{
	std::lock_guard lock(m_AllocationLock);
	if (!m_PersistentHandles.CanAllocate())
	{
		CleanupPersistent();
	}

	gAssert(m_PersistentHandles.CanAllocate(), "Out of persistent descriptor heap space (%d), increase heap size", m_NumPersistentDescriptors);

	return m_StartPtr.Offset(m_PersistentHandles.Allocate(), m_DescriptorSize);
}

void GPUDescriptorHeap::FreePersistent(DescriptorHandle& handle)
{
	gAssert(handle.IsValid());
	std::lock_guard lock(m_AllocationLock);
	m_PersistentDeletionQueue.emplace(handle, GetParent()->GetFrameFence()->GetCurrentValue());
	handle.Reset();
}

DescriptorHeapPage* GPUDescriptorHeap::AllocateScratchPage()
{
	std::lock_guard lock(m_ScratchPageAllocationMutex);
	if (m_FreeScratchPages.empty())
	{
		CleanupScratch();
	}

	gAssert(!m_FreeScratchPages.empty(), "Ran out of scratch descriptor heap space (%d). Increase heap size.", m_NumScratchDescriptors);
	DescriptorHeapPage* pPage = m_FreeScratchPages.back();
	m_FreeScratchPages.pop_back();
	return pPage;
}

void GPUDescriptorHeap::FreeScratchPage(const SyncPoint& syncPoint, DescriptorHeapPage* pPage)
{
	std::lock_guard lock(m_ScratchPageAllocationMutex);
	pPage->SyncPoint = syncPoint;
	pPage->CurrentOffset = 0;
	m_ReleasedScratchPages.push(pPage);
}

void GPUDescriptorHeap::CleanupScratch()
{
	while (!m_ReleasedScratchPages.empty())
	{
		DescriptorHeapPage* pPage = m_ReleasedScratchPages.front();
		if (!pPage->SyncPoint.IsComplete())
			break;

		m_ReleasedScratchPages.pop();
		m_FreeScratchPages.push_back(pPage);
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

GPUDescriptorHeap::Stats GPUDescriptorHeap::GetStats() const
{
	return Stats
	{
		.NumPersistentAllocations = m_PersistentHandles.GetNumAllocations(),
		.PersistentCapacity = m_PersistentHandles.GetCapacity(),
		.NumScratchAllocations = (uint32)m_ScratchPages.size() * m_ScratchPageSize,
		.ScratchCapacity = m_NumScratchDescriptors
	};
}

GPUScratchDescriptorAllocator::GPUScratchDescriptorAllocator(GPUDescriptorHeap* pGlobalHeap)
	: DeviceObject(pGlobalHeap->GetParent()), m_Type(pGlobalHeap->GetType()), m_pHeapAllocator(pGlobalHeap)
{
}

GPUScratchDescriptorAllocator::~GPUScratchDescriptorAllocator()
{
	if (m_pCurrentHeapPage)
	{
		m_ReleasedPages.push_back(m_pCurrentHeapPage);
	}
	Fence* pFrameFence = GetParent()->GetFrameFence();
	SyncPoint syncPoint(pFrameFence, pFrameFence->GetLastSignaledValue());
	ReleaseUsedHeaps(syncPoint);
}

void GPUScratchDescriptorAllocator::SetDescriptors(uint32 rootIndex, uint32 offset, Span<DescriptorHandle> handles)
{
	m_StaleRootParameters.SetBit(rootIndex);

	GraphicsDevice* pDevice = GetParent();
	StagedDescriptorTable& table = m_StagedDescriptors[rootIndex];
	gAssert(table.Capacity != 0, "Root parameter at index '%d' is not a descriptor table", rootIndex);
	gAssert(offset + handles.GetSize() <= table.Capacity, "Descriptor table at root index '%d' is too small (is %d but requires %d)", rootIndex, table.Capacity, offset + handles.GetSize());

	table.Descriptors.resize(Math::Max((uint32)table.Descriptors.size(), offset + handles.GetSize()));
	table.StartIndex = Math::Min(offset, table.StartIndex);
	for (int i = 0; i < (int)handles.GetSize(); ++i)
	{
		gAssert(handles[i]);
		table.Descriptors[offset + i] = pDevice->FindResourceDescriptorPtr(handles[i]).CPUOpaqueHandle;
	}
}

void GPUScratchDescriptorAllocator::BindStagedDescriptors(CommandContext& context, CommandListContext descriptorTableType)
{
	for (uint32 rootIndex : m_StaleRootParameters)
	{
		StagedDescriptorTable& table = m_StagedDescriptors[rootIndex];
		DescriptorPtr ptr = Allocate((uint32)table.Descriptors.size());
		for (int i = table.StartIndex; i < table.Descriptors.size(); ++i)
		{
			if (table.Descriptors[i].ptr != ~0 && table.Descriptors[i].ptr != 0)
			{
				DescriptorPtr target = ptr.Offset(i, m_pHeapAllocator->GetDescriptorSize());
				GetParent()->GetDevice()->CopyDescriptorsSimple(1, target.CPUHandle, table.Descriptors[i], m_Type);
			}
		}
		table.Descriptors.clear();
		table.StartIndex = 0xFFFFFFFF;

		if (descriptorTableType == CommandListContext::Graphics)
			context.GetCommandList()->SetGraphicsRootDescriptorTable(rootIndex, ptr.GPUHandle);
		else if (descriptorTableType == CommandListContext::Compute)
			context.GetCommandList()->SetComputeRootDescriptorTable(rootIndex, ptr.GPUHandle);
		else
			gUnreachable();
	}
	m_StaleRootParameters.ClearAll();
}

void GPUScratchDescriptorAllocator::ParseRootSignature(const RootSignature* pRootSignature)
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

void GPUScratchDescriptorAllocator::ReleaseUsedHeaps(const SyncPoint& syncPoint)
{
	for (DescriptorHeapPage* pPage : m_ReleasedPages)
		m_pHeapAllocator->FreeScratchPage(syncPoint, pPage);
	m_ReleasedPages.clear();
}

DescriptorPtr GPUScratchDescriptorAllocator::Allocate(uint32 descriptorCount)
{
	if (!m_pCurrentHeapPage || m_pCurrentHeapPage->Size - m_pCurrentHeapPage->CurrentOffset < descriptorCount)
	{
		if (m_pCurrentHeapPage)
			m_ReleasedPages.push_back(m_pCurrentHeapPage);
		m_pCurrentHeapPage = m_pHeapAllocator->AllocateScratchPage();
	}

	DescriptorPtr ptr = m_pCurrentHeapPage->StartPtr.Offset(m_pCurrentHeapPage->CurrentOffset, m_pHeapAllocator->GetDescriptorSize());
	m_pCurrentHeapPage->CurrentOffset += descriptorCount;
	return ptr;
}
