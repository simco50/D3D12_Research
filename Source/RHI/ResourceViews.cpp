#include "stdafx.h"
#include "ResourceViews.h"
#include "Device.h"
#include "Buffer.h"
#include "Texture.h"
#include "CPUDescriptorHeap.h"

ResourceView::ResourceView(DeviceResource* pParent, D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptor, DescriptorHandle gpuDescriptor)
	: DeviceObject(pParent->GetParent()), m_pResource(pParent), m_Descriptor(cpuDescriptor), m_GpuDescriptor(gpuDescriptor)
{
}

ResourceView::~ResourceView()
{
	if (m_Descriptor.ptr != 0)
	{
		GetParent()->FreeCPUDescriptor(m_Descriptor);
		GetParent()->UnregisterGlobalResourceView(m_GpuDescriptor);
	}
}

ShaderResourceView::ShaderResourceView(DeviceResource* pParent, D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptor, DescriptorHandle gpuDescriptor)
	: ResourceView(pParent, cpuDescriptor, gpuDescriptor)
{
}

UnorderedAccessView::UnorderedAccessView(DeviceResource* pParent, D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptor, DescriptorHandle gpuDescriptor)
	: ResourceView(pParent, cpuDescriptor, gpuDescriptor)
{
}
