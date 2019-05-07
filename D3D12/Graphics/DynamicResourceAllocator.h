#pragma once
class Graphics;
class GraphicsBuffer;

struct DynamicAllocation
{
	GraphicsBuffer* pBackingResource = nullptr;
	D3D12_GPU_VIRTUAL_ADDRESS GpuHandle{ 0 };
	int Offset = 0;
	int Size = 0;
	void* pMappedMemory = nullptr;
};

using AllocationPage = GraphicsBuffer;

class DynamicAllocationManager
{
public:
	DynamicAllocationManager(Graphics* pGraphics);
	~DynamicAllocationManager();

	AllocationPage* AllocatePage(uint64 size);
	AllocationPage* CreateNewPage(uint64 size);

	void FreePages(uint64 fenceValue, const std::vector<AllocationPage*> pPages);
	void FreeLargePages(uint64 fenceValue, const std::vector<AllocationPage*> pLargePages);

private:
	Graphics* m_pGraphics;
	std::mutex m_PageMutex;
	std::vector<std::unique_ptr<AllocationPage>> m_Pages;
	std::queue<std::pair<uint64, AllocationPage*>> m_FreedPages;
	std::queue<std::pair<uint64, std::unique_ptr<AllocationPage>>> m_DeleteQueue;
};

class DynamicResourceAllocator
{
public:
	DynamicResourceAllocator(DynamicAllocationManager* pPageManager);
	DynamicAllocation Allocate(uint64 size, int alignment = 256);
	void Free(uint64 fenceValue);

private:
	constexpr static uint64 PAGE_SIZE = 0xFFFF;

	DynamicAllocationManager* m_pPageManager;

	AllocationPage* m_pCurrentPage = nullptr;
	int m_CurrentOffset = 0;
	std::vector<AllocationPage*> m_UsedPages;
	std::vector<AllocationPage*> m_UsedLargePages;
};