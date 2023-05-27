#pragma once
#include "Buffer.h"
#include "Fence.h"

class SyncPoint;

struct DynamicAllocation
{
	RefCountPtr<Buffer> pBackingResource;
	D3D12_GPU_VIRTUAL_ADDRESS GpuHandle{ 0 };
	uint64 Offset = 0;
	uint64 Size = 0;
	void* pMappedMemory = nullptr;
	void Clear(uint32 value = 0)
	{
		memset(pMappedMemory, value, Size);
	}
};

class DynamicAllocationManager : public GraphicsObject
{
public:
	DynamicAllocationManager(GraphicsDevice* pParent, BufferFlag bufferFlags);

	RefCountPtr<Buffer> AllocatePage(uint64 size);
	RefCountPtr<Buffer> CreateNewPage(const char* pName, uint64 size);

	void FreePages(const SyncPoint& syncPoint, const std::vector<RefCountPtr<Buffer>>& pPages);

private:
	BufferFlag m_BufferFlags;
	FencedPool<RefCountPtr<Buffer>, true> m_PagePool;
};

class DynamicResourceAllocator
{
public:
	DynamicResourceAllocator(DynamicAllocationManager* pPageManager);
	DynamicAllocation Allocate(uint64 size, int alignment);
	void Free(const SyncPoint& syncPoint);

private:
	DynamicAllocationManager* m_pPageManager;

	RefCountPtr<Buffer> m_pCurrentPage;
	uint64 m_CurrentOffset = 0;
	std::vector<RefCountPtr<Buffer>> m_UsedPages;
};
