#include "stdafx.h"
#include "DynamicResourceAllocator.h"
#include "Graphics.h"
#include "GraphicsBuffer.h"

DynamicResourceAllocator::DynamicResourceAllocator(DynamicAllocationManager* pPageManager)
	: m_pPageManager(pPageManager)
{

}

DynamicAllocation DynamicResourceAllocator::Allocate(uint64 size, int alignment)
{
	int bufferSize = (size + (alignment - 1)) & ~(alignment - 1);
	DynamicAllocation allocation;
	allocation.Size = bufferSize;

	if (bufferSize > PAGE_SIZE)
	{
		AllocationPage* pPage = m_pPageManager->CreateNewPage(bufferSize);
		m_UsedLargePages.push_back(pPage);
		allocation.Offset = 0;
		allocation.GpuHandle = pPage->GetGpuHandle();
		allocation.pBackingResource = pPage;
		allocation.pMappedMemory = pPage->GetMappedData();
	}
	else
	{
		if (m_pCurrentPage == nullptr || m_CurrentOffset + bufferSize > PAGE_SIZE)
		{
			m_pCurrentPage = m_pPageManager->AllocatePage(PAGE_SIZE);
			m_CurrentOffset = 0;
			m_UsedPages.push_back(m_pCurrentPage);
		}
		allocation.Offset = m_CurrentOffset;
		allocation.GpuHandle = m_pCurrentPage->GetGpuHandle() + m_CurrentOffset;
		allocation.pBackingResource = m_pCurrentPage;
		allocation.pMappedMemory = (char*)m_pCurrentPage->GetMappedData() + m_CurrentOffset;

		m_CurrentOffset += bufferSize;
	}
	return allocation;
}

void DynamicResourceAllocator::Free(uint64 fenceValue)
{
	m_pPageManager->FreePages(fenceValue, m_UsedPages);
	m_UsedPages.clear();

	m_pPageManager->FreeLargePages(fenceValue, m_UsedLargePages);
	m_UsedLargePages.clear();

	m_pCurrentPage = nullptr;
	m_CurrentOffset = 0;
}

DynamicAllocationManager::DynamicAllocationManager(Graphics* pGraphics)
	: m_pGraphics(pGraphics)
{

}

DynamicAllocationManager::~DynamicAllocationManager()
{

}

AllocationPage* DynamicAllocationManager::AllocatePage(uint64 size)
{
	std::lock_guard<std::mutex> lockGuard(m_PageMutex);

	AllocationPage* pPage = nullptr;
	if (m_FreedPages.size() > 0 && m_pGraphics->IsFenceComplete(m_FreedPages.front().first))
	{
		pPage = m_FreedPages.front().second;
		m_FreedPages.pop();
	}
	else
	{
		pPage = CreateNewPage(size);
		m_Pages.emplace_back(pPage);
	}
	return pPage;
}

AllocationPage* DynamicAllocationManager::CreateNewPage(uint64 size)
{
	AllocationPage* pNewPage = new AllocationPage();
	pNewPage->Create(m_pGraphics, size, true);
	pNewPage->Map();
	return pNewPage;
}

void DynamicAllocationManager::FreePages(uint64 fenceValue, const std::vector<AllocationPage*> pPages)
{
	std::lock_guard<std::mutex> lockGuard(m_PageMutex);
	for (AllocationPage* pPage : pPages)
	{
		m_FreedPages.emplace(fenceValue, pPage);
	}
}

void DynamicAllocationManager::FreeLargePages(uint64 fenceValue, const std::vector<AllocationPage*> pLargePages)
{
	std::lock_guard<std::mutex> lockGuard(m_PageMutex);

	while (m_DeleteQueue.size() > 0 && m_pGraphics->IsFenceComplete(m_DeleteQueue.front().first))
	{
		m_DeleteQueue.pop();
	}

	for (AllocationPage* pPage : pLargePages)
	{
		m_DeleteQueue.emplace(fenceValue, pPage);
	}
}
