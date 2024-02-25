#pragma once
#include "Buffer.h"
#include "Fence.h"

class SyncPoint;

struct ScratchAllocation
{
	Ref<Buffer> pBackingResource;
	D3D12_GPU_VIRTUAL_ADDRESS GpuHandle{ 0 };
	uint64 Offset = 0;
	uint64 Size = 0;
	void* pMappedMemory = nullptr;
	void Clear(uint32 value = 0)
	{
		memset(pMappedMemory, value, Size);
	}
};

class ScratchAllocationManager : public DeviceObject
{
public:
	ScratchAllocationManager(GraphicsDevice* pParent, BufferFlag bufferFlags, uint64 pageSize);

	Ref<Buffer> AllocatePage();
	void FreePages(const SyncPoint& syncPoint, const std::vector<Ref<Buffer>>& pPages);
	uint64 GetPageSize() const { return m_PageSize; }

private:
	BufferFlag m_BufferFlags;
	uint64 m_PageSize;
	FencedPool<Ref<Buffer>, true> m_PagePool;
};

class ScratchAllocator
{
public:
	ScratchAllocator(ScratchAllocationManager* pPageManager);
	ScratchAllocation Allocate(uint64 size, int alignment);
	void Free(const SyncPoint& syncPoint);

private:
	ScratchAllocationManager* m_pPageManager;

	Ref<Buffer> m_pCurrentPage;
	uint64 m_CurrentOffset = 0;
	std::vector<Ref<Buffer>> m_UsedPages;
};
