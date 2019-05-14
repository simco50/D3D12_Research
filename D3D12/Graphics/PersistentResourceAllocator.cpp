#include "stdafx.h"
#include "PersistentResourceAllocator.h"

PersistentResourceAllocatorHeap::PersistentResourceAllocatorHeap(ID3D12Device* pDevice, D3D12_HEAP_FLAGS flags, uint64 heapSize)
	: m_pDevice(pDevice), m_HeapFlags(flags), m_HeapSize(heapSize)
{

}

PersistentResourceAllocatorHeap::~PersistentResourceAllocatorHeap()
{
	for (ID3D12Heap* pHeap : m_Heaps)
	{
		pHeap->Release();
	}
}

ID3D12Resource* PersistentResourceAllocatorHeap::CreateResource(const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES initialState, D3D12_CLEAR_VALUE* clearValue /*= nullptr*/)
{
	D3D12_RESOURCE_ALLOCATION_INFO info = m_pDevice->GetResourceAllocationInfo(0, 1, &desc);
	uint64 offset = Math::AlignUp(m_CurrentOffset, info.Alignment);
	if (m_pCurrentHeap == nullptr || offset + info.SizeInBytes > m_HeapSize)
	{
		m_pCurrentHeap = CreateNewHeap();
		m_Heaps.push_back(m_pCurrentHeap);
		offset = 0;
	}
	ID3D12Resource* pResource = nullptr;
	HR(m_pDevice->CreatePlacedResource(m_pCurrentHeap, offset, &desc, initialState, clearValue, IID_PPV_ARGS(&pResource)));
	m_CurrentOffset = offset + info.SizeInBytes;
	return pResource;
}

ID3D12Heap* PersistentResourceAllocatorHeap::CreateNewHeap()
{
	ID3D12Heap* pHeap = nullptr;
	CD3DX12_HEAP_DESC desc = CD3DX12_HEAP_DESC(m_HeapSize, D3D12_HEAP_TYPE_DEFAULT, 4194304, m_HeapFlags);
	m_pDevice->CreateHeap(&desc, IID_PPV_ARGS(&pHeap));
	return pHeap;
}

PersistentResourceAllocator::PersistentResourceAllocator(ID3D12Device* pDevice)
{
	m_Allocators[(int)ResourceType::Buffer] = std::make_unique<PersistentResourceAllocatorHeap>(pDevice, D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS, 0x10000000);
	m_Allocators[(int)ResourceType::Texture] = std::make_unique<PersistentResourceAllocatorHeap>(pDevice, D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES, 0x10000000);
	m_Allocators[(int)ResourceType::RenderTarget] = std::make_unique<PersistentResourceAllocatorHeap>(pDevice, D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES, 0x8000000);
}

ID3D12Resource* PersistentResourceAllocator::CreateResource(const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES initialState, D3D12_CLEAR_VALUE* clearValue /*= nullptr*/)
{
	switch (desc.Dimension)
	{
	case D3D12_RESOURCE_DIMENSION_BUFFER:
		return m_Allocators[(int)ResourceType::Buffer]->CreateResource(desc, initialState, clearValue);
	case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
	case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
	case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
		if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) || (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))
		{
			return m_Allocators[(int)ResourceType::RenderTarget]->CreateResource(desc, initialState, clearValue);
		}
		else
		{
			return m_Allocators[(int)ResourceType::Texture]->CreateResource(desc, initialState, clearValue);
		}
	case D3D12_RESOURCE_DIMENSION_UNKNOWN:
	default:
		return nullptr;
	}
}
