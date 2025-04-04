#include "stdafx.h"
#include "GPUDescriptorHeap.h"
#include "Device.h"
#include "CommandQueue.h"

GPUDescriptorHeap::GPUDescriptorHeap(GraphicsDevice* pParent, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32 numDescriptors)
	: DeviceObject(pParent), m_Type(type), m_NumDescriptors(numDescriptors), m_Handles(numDescriptors)
{
	gAssert(type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV || type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, "Online Descriptor Heap must be either of CBV/SRV/UAV or Sampler type.");

	// Create a GPU visible descriptor heap
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc{
			.Type			= type,
			.NumDescriptors = numDescriptors,
			.Flags			= D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
			.NodeMask		= 0,
		};
		pParent->GetDevice()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(m_pHeap.GetAddressOf()));
		D3D::SetObjectName(m_pHeap.Get(), type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ? "GPU CBV/SRV/UAV Descriptor Heap" : "GPU Sampler Descriptor Heap");
	}
	// Create a CPU opaque descriptor heap for all persistent descriptors
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc{
			.Type			= type,
			.NumDescriptors = numDescriptors,
			.Flags			= D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
			.NodeMask		= 0,
		};
		pParent->GetDevice()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(m_pCPUHeap.GetAddressOf()));
		D3D::SetObjectName(m_pCPUHeap.Get(), type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ? "GPU CBV/SRV/UAV CPU Opaque Descriptor Heap" : "GPU Sampler CPU Opaque Descriptor Heap");
	}

	IF_DEBUG(m_Owners.resize(numDescriptors);)

	m_DescriptorSize = pParent->GetDevice()->GetDescriptorHandleIncrementSize(type);
	m_StartPtr		 = DescriptorPtr{
			  .CPUHandle	   = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_pHeap->GetCPUDescriptorHandleForHeapStart()),
			  .GPUHandle	   = CD3DX12_GPU_DESCRIPTOR_HANDLE(m_pHeap->GetGPUDescriptorHandleForHeapStart()),
			  .CPUOpaqueHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_pCPUHeap->GetCPUDescriptorHandleForHeapStart()),
			  .HeapIndex	   = 0
	};
}

GPUDescriptorHeap::~GPUDescriptorHeap()
{
	Cleanup();
}

DescriptorPtr GPUDescriptorHeap::Allocate(const DeviceResource* pOwner)
{
	std::lock_guard lock(m_AllocationLock);
	if (!m_Handles.CanAllocate())
		Cleanup();

	gAssert(m_Handles.CanAllocate(), "Out of persistent descriptor heap space (%d), increase heap size", m_NumDescriptors);

	DescriptorPtr ptr = m_StartPtr.Offset(m_Handles.Allocate(), m_DescriptorSize);
	IF_DEBUG(m_Owners[ptr.HeapIndex] = pOwner;)
	return ptr;
}

void GPUDescriptorHeap::Free(DescriptorHandle& handle)
{
	gAssert(handle.IsValid());
	std::lock_guard lock(m_AllocationLock);
	m_DeletionQueue.emplace(handle, GetParent()->GetFrameFence()->GetCurrentValue());
	IF_DEBUG(m_Owners[handle.HeapIndex] = nullptr;)
	handle.Reset();
}

void GPUDescriptorHeap::Cleanup()
{
	while (m_DeletionQueue.size())
	{
		const auto& f = m_DeletionQueue.front();
		if (!GetParent()->GetFrameFence()->IsComplete(f.second))
			break;

		m_Handles.Free(f.first);
		m_DeletionQueue.pop();
	}
}
