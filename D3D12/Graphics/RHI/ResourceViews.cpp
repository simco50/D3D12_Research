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
		GetParent()->FreeCPUDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_Descriptor);
		GetParent()->UnregisterGlobalResourceView(m_GpuDescriptor);
	}
}

ShaderResourceView::ShaderResourceView(GraphicsResource* pParent, D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptor, DescriptorHandle gpuDescriptor)
	: ResourceView(pParent, cpuDescriptor, gpuDescriptor)
{
}

UnorderedAccessView::UnorderedAccessView(GraphicsResource* pParent, D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptor, DescriptorHandle gpuDescriptor)
	: ResourceView(pParent, cpuDescriptor, gpuDescriptor)
{
}
