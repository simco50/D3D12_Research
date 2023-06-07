#pragma once
#include "GraphicsResource.h"

class CPUDescriptorHeap : public GraphicsObject
{
public:
	CPUDescriptorHeap(GraphicsDevice* pParent, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32 numDescriptors);

	CD3DX12_CPU_DESCRIPTOR_HANDLE AllocateDescriptor();
	void FreeDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE handle);
	D3D12_DESCRIPTOR_HEAP_TYPE GetType() const { return m_Type; }

private:

	RefCountPtr<ID3D12DescriptorHeap> m_pHeap;
	FreeList m_FreeList;
	uint32 m_NumDescriptors;
	uint32 m_DescriptorSize = 0;
	D3D12_DESCRIPTOR_HEAP_TYPE m_Type;
};
