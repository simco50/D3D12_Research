#pragma once
#include "GraphicsResource.h"

struct Heap : public GraphicsObject
{
	Heap(GraphicsDevice* pParent);

	struct Range
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE Begin;
		CD3DX12_CPU_DESCRIPTOR_HANDLE End;
	};

	RefCountPtr<ID3D12DescriptorHeap> pHeap;
	std::list<Range> FreeRanges;
};

class OfflineDescriptorAllocator : public GraphicsObject
{
public:
	OfflineDescriptorAllocator(GraphicsDevice* pParent, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32 descriptorsPerHeap);

	CD3DX12_CPU_DESCRIPTOR_HANDLE AllocateDescriptor();
	void FreeDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE handle);
	D3D12_DESCRIPTOR_HEAP_TYPE GetType() const { return m_Type; }

private:
	void AllocateNewHeap();

	std::vector<RefCountPtr<Heap>> m_Heaps;
	std::list<int> m_FreeHeaps;

	uint32 m_DescriptorsPerHeap;
	uint32 m_DescriptorSize = 0;
	D3D12_DESCRIPTOR_HEAP_TYPE m_Type;
};
