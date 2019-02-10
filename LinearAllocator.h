#pragma once
#include "GpuResource.h"

class Graphics;

struct DynamicAllocation
{
	DynamicAllocation(ID3D12Resource* pResource, size_t offset, size_t size):
		Resource(pResource), Offset(offset), Size(size)
	{}

	ID3D12Resource* Resource;
	size_t Offset;
	size_t Size;
	void* pCpuAddress;
	D3D12_GPU_VIRTUAL_ADDRESS GpuAddress;
};

class LinearAllocationPage : public GpuResource
{
public:
	LinearAllocationPage(ID3D12Resource* pResource, const size_t size, D3D12_RESOURCE_STATES usageState):
		GpuResource(usageState), m_Size(size)
	{
		m_pResource.Attach(pResource);
		m_pGpuAddress = pResource->GetGPUVirtualAddress();
		Map();
	}

	~LinearAllocationPage()
	{
		Unmap();
	}

	void Map()
	{
		if (m_pCpuAddress == nullptr)
		{
			m_pResource->Map(0, nullptr, &m_pCpuAddress);
		}
	}

	void Unmap()
	{
		if (m_pCpuAddress != nullptr)
		{
			m_pResource->Unmap(0, nullptr);
			m_pCpuAddress = nullptr;
		}
	}

	size_t GetSize() const { return m_Size; }

	void* m_pCpuAddress;
	D3D12_GPU_VIRTUAL_ADDRESS m_pGpuAddress;
	size_t m_Size;
};

enum class LinearAllocationType
{
	GpuExclusive,
	CpuWrite
};

class LinearAllocatorPageManager
{
public:
	LinearAllocatorPageManager(Graphics* pGraphics, const LinearAllocationType allocationType) :
		m_pGraphics(pGraphics), m_Type(allocationType)
	{}

	LinearAllocationPage* RequestPage();
	LinearAllocationPage* CreateNewPage(const size_t size);

private:
	Graphics* m_pGraphics;

	static const int CPU_PAGE_SIZE = 0x10000;
	static const int GPU_PAGE_SIZE = 0x200000;

	vector<unique_ptr<LinearAllocationPage>> m_PagePool;
	queue<LinearAllocationPage*> m_AvailablePages;
	queue<std::pair<UINT64, LinearAllocationPage*>> m_RetiredPages;
	queue<std::pair<UINT64, LinearAllocationPage*>> m_DeletionQueue;

	LinearAllocationType m_Type = LinearAllocationType::CpuWrite;
};

class LinearAllocator
{
public:
	LinearAllocator(Graphics* pGraphics);
	~LinearAllocator();

	DynamicAllocation Allocate(const LinearAllocationType type, size_t size, const size_t alignment = 0);
private:

	LinearAllocationPage* m_pCurrentPage = nullptr;
	unsigned int m_CurrentOffset = 0;
	unsigned int m_PageSize = 0;

	vector<unique_ptr<LinearAllocatorPageManager>> m_PagesManagers;
};

