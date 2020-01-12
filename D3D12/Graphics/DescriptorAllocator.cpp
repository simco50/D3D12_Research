#include "stdafx.h"
#include "DescriptorAllocator.h"

const int DescriptorAllocator::DESCRIPTORS_PER_HEAP = 256;

DescriptorAllocator::DescriptorAllocator(ID3D12Device* pDevice, D3D12_DESCRIPTOR_HEAP_TYPE type)
	: m_pDevice(pDevice), m_Type(type)
{
	m_DescriptorSize = pDevice->GetDescriptorHandleIncrementSize(type);
}

D3D12_CPU_DESCRIPTOR_HANDLE DescriptorAllocator::AllocateDescriptors(uint32 count)
{
	assert(count > 0);
	if (m_RemainingDescriptors <= count || m_DescriptorHeapPool.size() == 0)
	{
		AllocateNewHeap();
	}
	D3D12_CPU_DESCRIPTOR_HANDLE handle = m_CurrentCpuHandle;
	m_CurrentCpuHandle.Offset(count, m_DescriptorSize);
	m_RemainingDescriptors -= count;
	return handle;
}

void DescriptorAllocator::AllocateNewHeap()
{
	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	desc.NodeMask = 0;
	desc.NumDescriptors = DESCRIPTORS_PER_HEAP;
	desc.Type = m_Type;
	ComPtr<ID3D12DescriptorHeap> pNewHeap;
	m_pDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(pNewHeap.GetAddressOf()));
	m_DescriptorHeapPool.push_back(std::move(pNewHeap));
	m_CurrentCpuHandle = m_DescriptorHeapPool.back()->GetCPUDescriptorHandleForHeapStart();
	m_RemainingDescriptors = DESCRIPTORS_PER_HEAP;
}

uint32 DescriptorAllocator::GetNumAllocatedDescriptors() const
{
	return std::max((int)m_DescriptorHeapPool.size() - 1, 0) * DESCRIPTORS_PER_HEAP + DESCRIPTORS_PER_HEAP - m_RemainingDescriptors;
}