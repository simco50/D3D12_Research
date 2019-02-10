#include "stdafx.h"
#include "LinearAllocator.h"
#include "Graphics.h"

LinearAllocator::LinearAllocator(Graphics* pGraphics)
{
	m_PagesManagers.resize(2);
	m_PagesManagers[0] = make_unique<LinearAllocatorPageManager>(pGraphics, LinearAllocationType::GpuExclusive);
	m_PagesManagers[1] = make_unique<LinearAllocatorPageManager>(pGraphics, LinearAllocationType::CpuWrite);
}

LinearAllocator::~LinearAllocator()
{
}

DynamicAllocation LinearAllocator::Allocate(const LinearAllocationType type, size_t size, const size_t alignment)
{
	if(alignment)
		size = (size + alignment - 1) - (size + alignment - 1) % alignment;

	if (m_pCurrentPage == nullptr || m_CurrentOffset + size > m_pCurrentPage->GetSize())
	{
		m_pCurrentPage = m_PagesManagers[(int)type]->RequestPage();
		m_CurrentOffset = 0;
	}

	DynamicAllocation allocation(m_pCurrentPage->GetResource(), m_CurrentOffset, size);
	allocation.GpuAddress = m_pCurrentPage->m_pGpuAddress + m_CurrentOffset;
	allocation.pCpuAddress = (char*)m_pCurrentPage->m_pCpuAddress + m_CurrentOffset;

	m_CurrentOffset += size;

	return allocation;
}

LinearAllocationPage* LinearAllocatorPageManager::RequestPage()
{
	while (!m_RetiredPages.empty() && m_pGraphics->IsFenceComplete(m_RetiredPages.front().first))
	{
		m_AvailablePages.push(m_RetiredPages.front().second);
		m_RetiredPages.pop();
	}

	LinearAllocationPage* pPage = nullptr;

	if (!m_AvailablePages.empty())
	{
		pPage = m_AvailablePages.front();
		m_AvailablePages.pop();
	}
	else
	{
		pPage = CreateNewPage(0);
		m_PagePool.emplace_back(pPage);
	}
	return pPage;
}

LinearAllocationPage* LinearAllocatorPageManager::CreateNewPage(const size_t size)
{
	D3D12_HEAP_PROPERTIES heapProps;
	heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heapProps.CreationNodeMask = 1;
	heapProps.VisibleNodeMask = 1;

	D3D12_RESOURCE_DESC resourceDesc = {};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Alignment = 0;
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	resourceDesc.MipLevels = 1;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.SampleDesc.Quality = 0;

	D3D12_RESOURCE_STATES usage;

	if (m_Type == LinearAllocationType::GpuExclusive)
	{
		resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		resourceDesc.Width = size ? size : GPU_PAGE_SIZE;
		heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
		usage = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	}
	else
	{
		resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
		resourceDesc.Width = size ? size : CPU_PAGE_SIZE;
		heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
		usage = D3D12_RESOURCE_STATE_GENERIC_READ;
	}

	ID3D12Resource* pResource;
	m_pGraphics->GetDevice()->CreateCommittedResource(
		&heapProps, 
		D3D12_HEAP_FLAG_NONE, 
		&resourceDesc, 
		usage,
		nullptr, 
		IID_PPV_ARGS(&pResource)
	);

	pResource->SetName(L"Linear Allocator Page");

	return new LinearAllocationPage(pResource, (size_t)resourceDesc.Width, usage);
}
