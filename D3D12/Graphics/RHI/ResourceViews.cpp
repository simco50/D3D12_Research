#include "stdafx.h"
#include "ResourceViews.h"
#include "Graphics.h"
#include "Buffer.h"
#include "Texture.h"
#include "CPUDescriptorHeap.h"

ResourceView::ResourceView(GraphicsResource* pParent, D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptor, DescriptorHandle gpuDescriptor)
	: GraphicsObject(pParent->GetParent()), m_pResource(pParent), m_Descriptor(cpuDescriptor), m_GpuDescriptor(gpuDescriptor)
{
}

ResourceView::~ResourceView()
{
	if (m_Descriptor.ptr != 0)
	{
		check(m_pResource);
		GetParent()->FreeDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_Descriptor);
		m_Descriptor.ptr = 0;
		GetParent()->FreeViewDescriptor(m_GpuDescriptor);
	}
}

ShaderResourceView::ShaderResourceView(GraphicsResource* pParent, D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptor, DescriptorHandle gpuDescriptor)
	: ResourceView(pParent, cpuDescriptor, gpuDescriptor)
{
}

UnorderedAccessView::UnorderedAccessView(GraphicsResource* pParent, D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptor, DescriptorHandle gpuDescriptor, Buffer* pCounter)
	: ResourceView(pParent, cpuDescriptor, gpuDescriptor), m_pCounter(pCounter)
{
}

UnorderedAccessView* UnorderedAccessView::GetCounterUAV() const
{
	return m_pCounter ? m_pCounter->GetUAV() : nullptr;
}

ShaderResourceView* UnorderedAccessView::GetCounterSRV() const
{
	return m_pCounter ? m_pCounter->GetSRV() : nullptr;
}
