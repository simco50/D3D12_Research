#pragma once
#include "GraphicsResource.h"

class CPUDescriptorHeap : public GraphicsObject
{
public:
	CPUDescriptorHeap(GraphicsDevice* pParent, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32 descriptorsPerHeap);
	~CPUDescriptorHeap();

	CD3DX12_CPU_DESCRIPTOR_HANDLE AllocateDescriptor();
	void FreeDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE handle);
	D3D12_DESCRIPTOR_HEAP_TYPE GetType() const { return m_Type; }

private:
	void AllocateNewHeap();

	std::vector<RefCountPtr<ID3D12DescriptorHeap>> m_Heaps;
	FreeList<true> m_FreeList;
	uint32 m_DescriptorsPerHeap;
	uint32 m_DescriptorSize = 0;
	D3D12_DESCRIPTOR_HEAP_TYPE m_Type;
	std::mutex m_AllocationMutex;
};
