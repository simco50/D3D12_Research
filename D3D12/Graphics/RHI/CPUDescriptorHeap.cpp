#include "stdafx.h"
#include "CPUDescriptorHeap.h"
#include "Graphics.h"

CPUDescriptorHeap::CPUDescriptorHeap(GraphicsDevice* pParent, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32 descriptorsPerHeap)
	: GraphicsObject(pParent), m_FreeList(descriptorsPerHeap), m_DescriptorsPerHeap(descriptorsPerHeap), m_Type(type)
{
	m_DescriptorSize = pParent->GetDevice()->GetDescriptorHandleIncrementSize(type);
}

CPUDescriptorHeap::~CPUDescriptorHeap()
{
	//#todo: ImGui descriptor leaks
	//checkf(m_FreeList.GetNumAllocations() == 0, "Leaked descriptors");
}

CD3DX12_CPU_DESCRIPTOR_HANDLE CPUDescriptorHeap::AllocateDescriptor()
{
	std::lock_guard lock(m_AllocationMutex);

	CD3DX12_CPU_DESCRIPTOR_HANDLE outHandle{};
	uint32 index = m_FreeList.Allocate();
	uint32 heapIndex = index / m_DescriptorsPerHeap;
	uint32 elementIndex = index % m_DescriptorsPerHeap;
	if (heapIndex >= m_Heaps.size())
	{
		AllocateNewHeap();
	}
	outHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_Heaps[heapIndex]->GetCPUDescriptorHandleForHeapStart());
	outHandle.Offset(elementIndex, m_DescriptorSize);
	return outHandle;
}

void CPUDescriptorHeap::FreeDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
	std::lock_guard lock(m_AllocationMutex);
	int heapIndex = -1;

	for (size_t i = 0; i < m_Heaps.size(); ++i)
	{
		D3D12_CPU_DESCRIPTOR_HANDLE start = m_Heaps[i]->GetCPUDescriptorHandleForHeapStart();
		bool isInRange = handle.ptr >= start.ptr && handle.ptr < start.ptr + m_DescriptorSize * m_DescriptorsPerHeap;
		bool isAligned = (handle.ptr - start.ptr) % m_DescriptorSize == 0;
		if (isInRange && isAligned)
		{
			heapIndex = (int)i;
			break;
		}
	}

	check(heapIndex >= 0);
	ID3D12DescriptorHeap* pHeap = m_Heaps[heapIndex];
	uint32 elementIndex = (uint32)((handle.ptr - pHeap->GetCPUDescriptorHandleForHeapStart().ptr) / m_DescriptorSize);
	m_FreeList.Free(heapIndex * m_DescriptorsPerHeap + elementIndex);
}

void CPUDescriptorHeap::AllocateNewHeap()
{
	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	desc.NodeMask = 0;
	desc.NumDescriptors = m_DescriptorsPerHeap;
	desc.Type = m_Type;

	RefCountPtr<ID3D12DescriptorHeap> pHeap;
	GetParent()->GetDevice()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(pHeap.GetAddressOf()));
	D3D::SetObjectName(pHeap, "Offline Descriptor Heap");
	m_Heaps.push_back(pHeap);
}
