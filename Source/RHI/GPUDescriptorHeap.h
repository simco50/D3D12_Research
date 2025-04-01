#pragma once
#include "DescriptorHandle.h"
#include "DeviceResource.h"

class GPUDescriptorHeap : public DeviceObject
{
public:
	GPUDescriptorHeap(GraphicsDevice* pParent, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32 numDescriptors);
	~GPUDescriptorHeap();

	DescriptorPtr				Allocate(const DeviceResource* pOwner);
	void						Free(DescriptorHandle& handle);

	uint32						GetNumAllocations() const		{ return m_Handles.GetNumAllocations(); }
	uint32						GetCapacity() const				{ return m_Handles.GetCapacity(); }

	uint32						GetDescriptorSize() const		{ return m_DescriptorSize; }
	ID3D12DescriptorHeap*		GetHeap() const					{ return m_pHeap.Get(); }
	D3D12_DESCRIPTOR_HEAP_TYPE	GetType() const					{ return m_Type; }
	DescriptorPtr				GetStartPtr() const				{ return m_StartPtr; }

private:
	void Cleanup();

	Ref<ID3D12DescriptorHeap>						m_pHeap;
	Ref<ID3D12DescriptorHeap>						m_pCPUHeap;
	D3D12_DESCRIPTOR_HEAP_TYPE						m_Type;
	uint32											m_DescriptorSize = 0;
	DescriptorPtr									m_StartPtr;
	FreeList										m_Handles;
	uint32											m_NumDescriptors;
	std::mutex										m_AllocationLock;
	std::queue<std::pair<DescriptorHandle, uint64>> m_DeletionQueue;
#ifdef _DEBUG
	Array<const DeviceResource*>					m_Owners;
#endif
};
