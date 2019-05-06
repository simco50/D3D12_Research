#pragma once
#include "DescriptorHandle.h"

class DescriptorAllocator
{
public:
	DescriptorAllocator(ID3D12Device* pDevice, D3D12_DESCRIPTOR_HEAP_TYPE type, bool gpuVisible = false);
	~DescriptorAllocator() = default;

	D3D12_CPU_DESCRIPTOR_HANDLE AllocateDescriptors(int count);
	ID3D12DescriptorHeap* GetCurrentHeap() { return m_DescriptorHeapPool.back().Get(); }

	uint32 GetHeapCount() const { return (uint32)m_DescriptorHeapPool.size(); }
	uint32 GetNumAllocatedDescriptors() const { return std::max((int)m_DescriptorHeapPool.size() - 1, 0) * DESCRIPTORS_PER_HEAP + DESCRIPTORS_PER_HEAP - m_RemainingDescriptors; }
	D3D12_DESCRIPTOR_HEAP_TYPE GetType() const { return m_Type; }
	static const int DESCRIPTORS_PER_HEAP = 256;

private:
	void AllocateNewHeap();

	bool m_GpuVisible;
	std::vector<ComPtr<ID3D12DescriptorHeap>> m_DescriptorHeapPool;
	CD3DX12_CPU_DESCRIPTOR_HANDLE m_CurrentCpuHandle;
	ID3D12Device* m_pDevice;
	D3D12_DESCRIPTOR_HEAP_TYPE m_Type;
	uint32 m_DescriptorSize = 0;
	uint32 m_RemainingDescriptors = DESCRIPTORS_PER_HEAP;
};
