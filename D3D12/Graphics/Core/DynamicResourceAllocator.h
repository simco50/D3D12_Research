#pragma once
#include "Buffer.h"

struct DynamicAllocation
{
	Buffer* pBackingResource = nullptr;
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
	~DynamicAllocationManager();

	Buffer* AllocatePage(uint64 size);
	Buffer* CreateNewPage(uint64 size);

	void FreePages(uint64 fenceValue, const std::vector<Buffer*> pPages);
	void FreeLargePages(uint64 fenceValue, const std::vector<Buffer*> pLargePages);

private:
	BufferFlag m_BufferFlags;
	std::mutex m_PageMutex;
	std::vector<std::unique_ptr<Buffer>> m_Pages;
	std::queue<std::pair<uint64, Buffer*>> m_FreedPages;
	std::queue<std::pair<uint64, std::unique_ptr<Buffer>>> m_DeleteQueue;
};

class DynamicResourceAllocator
{
public:
	DynamicResourceAllocator(DynamicAllocationManager* pPageManager);
	DynamicAllocation Allocate(uint64 size, int alignment = 256);
	void Free(uint64 fenceValue);

private:
	DynamicAllocationManager* m_pPageManager;

	Buffer* m_pCurrentPage = nullptr;
	uint64 m_CurrentOffset = 0;
	std::vector<Buffer*> m_UsedPages;
	std::vector<Buffer*> m_UsedLargePages;
};
