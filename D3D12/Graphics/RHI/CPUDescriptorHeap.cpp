#include "stdafx.h"
#include "CPUDescriptorHeap.h"
#include "Graphics.h"

CPUDescriptorHeap::CPUDescriptorHeap(GraphicsDevice* pParent, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32 numDescriptors)
	: GraphicsObject(pParent), m_FreeList(numDescriptors), m_NumDescriptors(numDescriptors), m_Type(type)
{
	m_DescriptorSize = pParent->GetDevice()->GetDescriptorHandleIncrementSize(type);

	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	desc.NodeMask = 0;
	desc.NumDescriptors = numDescriptors;
	desc.Type = m_Type;

	GetParent()->GetDevice()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(m_pHeap.GetAddressOf()));
	D3D::SetObjectName(m_pHeap, "Offline Descriptor Heap");
}

CD3DX12_CPU_DESCRIPTOR_HANDLE CPUDescriptorHeap::AllocateDescriptor()
{
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(m_pHeap->GetCPUDescriptorHandleForHeapStart(), m_FreeList.Allocate(), m_DescriptorSize);
}

void CPUDescriptorHeap::FreeDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
	check(handle.ptr != DescriptorHandle::InvalidCPUHandle.ptr);
	uint32 elementIndex = (uint32)((handle.ptr - m_pHeap->GetCPUDescriptorHandleForHeapStart().ptr) / m_DescriptorSize);
	m_FreeList.Free(elementIndex);
}
