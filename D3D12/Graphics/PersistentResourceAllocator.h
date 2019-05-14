#pragma once

class PersistentResourceAllocatorHeap
{
public:
	PersistentResourceAllocatorHeap(ID3D12Device* pDevice, D3D12_HEAP_FLAGS flags, uint64 heapSize);
	~PersistentResourceAllocatorHeap();
	ID3D12Resource* CreateResource(const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES initialState, D3D12_CLEAR_VALUE* clearValue = nullptr);

	size_t GetHeapCount() const { return m_Heaps.size(); }
	uint64 GetRemainingSize() const { return m_HeapSize - m_CurrentOffset; }
	uint64 GetTotalSize() const { return m_HeapSize * m_Heaps.size(); }

private:
	ID3D12Heap* CreateNewHeap();

	ID3D12Device* m_pDevice;

	std::vector<ID3D12Heap*> m_Heaps;
	ID3D12Heap* m_pCurrentHeap = nullptr;
	uint64 m_CurrentOffset = 0;

	uint64 m_HeapSize;
	D3D12_HEAP_FLAGS m_HeapFlags;
};

enum class ResourceType
{
	Buffer,
	Texture,
	RenderTarget,
	MAX
};

class PersistentResourceAllocator
{
public:
	PersistentResourceAllocator(ID3D12Device* pDevice);
	ID3D12Resource* CreateResource(const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES initialState, D3D12_CLEAR_VALUE* clearValue = nullptr);

	uint64 GetRemainingSize(ResourceType type) const { return m_Allocators[(int)type]->GetRemainingSize(); }
	uint64 GetTotalSize(ResourceType type) const { return m_Allocators[(int)type]->GetTotalSize(); }
	uint64 GetHeapCount(ResourceType type) const { return m_Allocators[(int)type]->GetHeapCount(); }

private:
	std::array<std::unique_ptr<PersistentResourceAllocatorHeap>, 3> m_Allocators;
};