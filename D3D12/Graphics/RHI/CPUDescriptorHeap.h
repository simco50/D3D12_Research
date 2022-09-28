#pragma once
#include "GraphicsResource.h"

template<bool ThreadSafe>
struct FreeList
{
public:
	FreeList(uint32 chunkSize)
		: m_NumAllocations(0), m_ChunkSize(chunkSize)
	{}

	uint32 Allocate()
	{
		std::scoped_lock lock(m_Mutex);
		if (m_NumAllocations + 1 > m_FreeList.size())
		{
			uint32 size = (uint32)m_FreeList.size();
			m_FreeList.resize(size + m_ChunkSize);
			std::generate(m_FreeList.begin() + size, m_FreeList.end(), [&size]() { return size++; });
		}
		return m_FreeList[m_NumAllocations++];
	}

	void Free(uint32 index)
	{
		std::scoped_lock lock(m_Mutex);
		check(m_NumAllocations > 0);
		--m_NumAllocations;
		m_FreeList[m_NumAllocations] = index;
	}

	uint32 GetNumAllocations() const { return m_NumAllocations; }

private:
	struct DummyMutex
	{
		void lock() {}
		void unlock() {}
	};
	using TMutex = std::conditional_t<ThreadSafe, std::mutex, DummyMutex>;

	std::vector<uint32> m_FreeList;
	uint32 m_NumAllocations;
	uint32 m_ChunkSize;
	TMutex m_Mutex;
};

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
