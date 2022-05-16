#include "stdafx.h"
#include "DynamicResourceAllocator.h"
#include "Graphics.h"
#include "Buffer.h"

constexpr static uint64 PAGE_SIZE = Math::MegaBytesToBytes * 2;

DynamicAllocationManager::DynamicAllocationManager(GraphicsDevice* pParent, BufferFlag bufferFlags)
	: GraphicsObject(pParent), m_BufferFlags(bufferFlags)
{
}

RefCountPtr<Buffer> DynamicAllocationManager::AllocatePage(uint64 size)
{
	auto AllocatePage = [this, size]() {
		std::string name = Sprintf("Dynamic Allocation Buffer (%f KB)", Math::BytesToKiloBytes * size);
		return CreateNewPage(name.c_str(), size);
	};
	return m_PagePool.Allocate(AllocatePage);
}

RefCountPtr<Buffer> DynamicAllocationManager::CreateNewPage(const char* pName, uint64 size)
{
	return GetParent()->CreateBuffer(BufferDesc::CreateBuffer((uint32)size, m_BufferFlags), pName);
}

void DynamicAllocationManager::FreePages(const SyncPoint& syncPoint, const std::vector<RefCountPtr<Buffer>>& pPages)
{
	for (auto pPage : pPages)
	{
		m_PagePool.Free(std::move(pPage), syncPoint);
	}
}

DynamicResourceAllocator::DynamicResourceAllocator(DynamicAllocationManager* pPageManager)
	: m_pPageManager(pPageManager)
{
}

DynamicAllocation DynamicResourceAllocator::Allocate(uint64 size, int alignment)
{
	uint64 bufferSize = Math::AlignUp<uint64>(size, alignment);
	DynamicAllocation allocation;
	allocation.Size = bufferSize;

	if (bufferSize > PAGE_SIZE)
	{
		RefCountPtr<Buffer> pPage = m_pPageManager->CreateNewPage("Large Page", size);
		allocation.Offset = 0;
		allocation.GpuHandle = pPage->GetGpuHandle();
		allocation.pBackingResource = pPage;
		allocation.pMappedMemory = pPage->GetMappedData();
	}
	else
	{
		m_CurrentOffset = Math::AlignUp<uint64>(m_CurrentOffset, alignment);

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

void DynamicResourceAllocator::Free(const SyncPoint& syncPoint)
{
	m_pPageManager->FreePages(syncPoint, m_UsedPages);
	m_UsedPages.clear();

	m_pCurrentPage = nullptr;
	m_CurrentOffset = 0;
}
