#include "stdafx.h"
#include "DynamicResourceAllocator.h"
#include "Graphics.h"
#include "GraphicsBuffer.h"

constexpr static uint64 PAGE_SIZE = Math::MegaBytesToBytes * 2;

DynamicResourceAllocator::DynamicResourceAllocator(DynamicAllocationManager* pPageManager)
	: m_pPageManager(pPageManager)
{

}

DynamicAllocation DynamicResourceAllocator::Allocate(size_t size, int alignment)
{
	size_t bufferSize = Math::AlignUp<size_t>(size, alignment);
	DynamicAllocation allocation;
	allocation.Size = bufferSize;

	if (bufferSize > PAGE_SIZE)
	{
		Buffer* pPage = m_pPageManager->CreateNewPage(bufferSize);
		m_UsedLargePages.push_back(pPage);
		allocation.Offset = 0;
		allocation.GpuHandle = pPage->GetGpuHandle();
		allocation.pBackingResource = pPage;
		allocation.pMappedMemory = pPage->GetMappedData();
	}
	else
	{
		m_CurrentOffset = Math::AlignUp<size_t>(m_CurrentOffset, alignment);

		if (m_pCurrentPage == nullptr || m_CurrentOffset + bufferSize >= PAGE_SIZE)
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

DynamicAllocationManager::DynamicAllocationManager(Graphics* pGraphics, BufferFlag bufferFlags)
	: GraphicsObject(pGraphics), m_BufferFlags(bufferFlags)
{

}

DynamicAllocationManager::~DynamicAllocationManager()
{

}

Buffer* DynamicAllocationManager::AllocatePage(size_t size)
{
	std::lock_guard<std::mutex> lockGuard(m_PageMutex);

	Buffer* pPage = nullptr;
	if (m_FreedPages.size() > 0 && GetParent()->IsFenceComplete(m_FreedPages.front().first))
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

Buffer* DynamicAllocationManager::CreateNewPage(size_t size)
{
	Buffer* pNewPage = new Buffer(GetParent(), "Dynamic Allocation Buffer");
	pNewPage->Create(BufferDesc::CreateBuffer((uint32)size, m_BufferFlags));
	pNewPage->Map();
	return pNewPage;
}

void DynamicAllocationManager::FreePages(uint64 fenceValue, const std::vector<Buffer*> pPages)
{
	std::lock_guard<std::mutex> lockGuard(m_PageMutex);
	for (Buffer* pPage : pPages)
	{
		m_FreedPages.emplace(fenceValue, pPage);
	}
}

void DynamicAllocationManager::FreeLargePages(uint64 fenceValue, const std::vector<Buffer*> pLargePages)
{
	std::lock_guard<std::mutex> lockGuard(m_PageMutex);

	while (m_DeleteQueue.size() > 0 && GetParent()->IsFenceComplete(m_DeleteQueue.front().first))
	{
		m_DeleteQueue.pop();
	}

	for (Buffer* pPage : pLargePages)
	{
		m_DeleteQueue.emplace(fenceValue, pPage);
	}
}

void DynamicAllocationManager::CollectGarbage()
{
	std::lock_guard<std::mutex> lockGuard(m_PageMutex);
	GetParent()->IdleGPU();
	m_Pages.clear();
	m_FreedPages = {};
	m_DeleteQueue = {};
}

uint64 DynamicAllocationManager::GetMemoryUsage() const
{
	uint64 size = 0;
	for (const auto& pPage : m_Pages)
	{
		size += pPage->GetSize();
	}
	return size;
}
