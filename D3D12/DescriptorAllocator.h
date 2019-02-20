#pragma once
#include "DescriptorHandle.h"

class DescriptorAllocator
{
public:
	DescriptorAllocator(ID3D12Device* pDevice, D3D12_DESCRIPTOR_HEAP_TYPE type, bool gpuVisible = false);
	~DescriptorAllocator();

	DescriptorHandle AllocateDescriptor();
	ID3D12DescriptorHeap* GetCurrentHeap() { return m_DescriptorHeapPool.back().Get(); }

private:
	void AllocateNewHeap();
	static const int DESCRIPTORS_PER_HEAP = 64;

	bool m_GpuVisible;
	std::vector<ComPtr<ID3D12DescriptorHeap>> m_DescriptorHeapPool;
	CD3DX12_CPU_DESCRIPTOR_HANDLE m_CurrentCpuHandle;
	CD3DX12_GPU_DESCRIPTOR_HANDLE m_CurrentGpuHandle;
	ID3D12Device* m_pDevice;
	D3D12_DESCRIPTOR_HEAP_TYPE m_Type;
	uint32 m_DescriptorSize = 0;
	uint32 m_RemainingDescriptors = 0;
};
