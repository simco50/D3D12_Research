#include "stdafx.h"
#include "DynamicResourceAllocator.h"

DynamicResourceAllocator::DynamicResourceAllocator(ID3D12Device* pDevice, bool gpuVisible, int size)
	: m_pDevice(pDevice), m_Size(size)
{
	m_pBackingResource = CreateResource(pDevice, gpuVisible, size, &m_pMappedMemory);
}

DynamicAllocation DynamicResourceAllocator::Allocate(int size, int alignment)
{
	int bufferSize = (size + (alignment - 1)) & ~(alignment - 1);
	DynamicAllocation allocation;
	allocation.Size = bufferSize;

	if (size > m_Size)
	{
		m_LargeResources.emplace_back(CreateResource(m_pDevice, true, bufferSize, &allocation.pMappedMemory));
		allocation.pBackingResource = m_LargeResources.back().Get();
		allocation.Offset = 0;
		allocation.GpuHandle = allocation.pBackingResource->GetGPUVirtualAddress();
	}
	else
	{
		allocation.pBackingResource = m_pBackingResource.Get();

		m_CurrentOffset = ((size_t)m_CurrentOffset + (alignment - 1)) & ~(alignment - 1);

		if (bufferSize + m_CurrentOffset >= m_Size)
		{
			m_CurrentOffset = 0;
			if (m_FenceOffsets.size() > 0)
			{
				int maxOffset = m_FenceOffsets.front().second;
				assert(m_CurrentOffset + bufferSize <= maxOffset);
			}
		}
		allocation.GpuHandle = m_pBackingResource->GetGPUVirtualAddress() + m_CurrentOffset;
		allocation.Offset = m_CurrentOffset;
		allocation.pMappedMemory = static_cast<char*>(m_pMappedMemory) + m_CurrentOffset;
		m_CurrentOffset += bufferSize;
	}
	return allocation;
}

void DynamicResourceAllocator::Free(uint64 fenceValue)
{
	while (m_FenceOffsets.size() > 0)
	{
		const auto& offset = m_FenceOffsets.front();
		if (fenceValue > offset.first)
		{
			m_FenceOffsets.pop();
		}
		else
		{
			break;
		}
	}
	m_FenceOffsets.emplace(fenceValue, m_CurrentOffset);
}

ComPtr<ID3D12Resource> DynamicResourceAllocator::CreateResource(ID3D12Device* pDevice, bool gpuVisible, int size, void** pMappedData)
{
	ComPtr<ID3D12Resource> pResource;
	D3D12_RESOURCE_DESC desc = {};
	desc.Alignment = 0;
	desc.DepthOrArraySize = 1;
	desc.Dimension = D3D12_RESOURCE_DIMENSION_UNKNOWN;
	desc.Flags = D3D12_RESOURCE_FLAG_NONE;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.Height = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.MipLevels = 1;
	desc.Width = size;
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.SampleDesc.Count = 1;

	D3D12_HEAP_PROPERTIES props = {};
	props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	props.CreationNodeMask = 0;
	props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	props.Type = gpuVisible ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT;
	props.VisibleNodeMask = 0;

	HR(pDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(pResource.GetAddressOf())));

	D3D12_RANGE readRange;
	readRange.Begin = 0;
	readRange.End = 0;
	HR(pResource->Map(0, &readRange, pMappedData));
	return pResource;
}
