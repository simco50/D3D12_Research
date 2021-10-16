#pragma once

class DescriptorHandle
{
public:
	DescriptorHandle()
		: CpuHandle(InvalidCPUHandle), GpuHandle(InvalidGPUHandle), HeapIndex(InvalidHeapIndex)
	{

	}

	DescriptorHandle(
		D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
		int32 heapIndex,
		D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = InvalidGPUHandle)
		: CpuHandle(cpuHandle), GpuHandle(gpuHandle), HeapIndex(heapIndex)
	{
	}

	void OffsetInline(uint32 numDescriptors, uint32 descriptorSize)
	{
		if (CpuHandle != InvalidCPUHandle)
		{
			CpuHandle.Offset(numDescriptors, descriptorSize);
		}
		if (GpuHandle != InvalidGPUHandle)
		{
			GpuHandle.Offset(numDescriptors, descriptorSize);
		}
		if (HeapIndex != InvalidHeapIndex)
		{
			HeapIndex += numDescriptors;
		}
	}

	DescriptorHandle Offset(uint32 numDescriptors, uint32 descriptorSize)
	{
		DescriptorHandle handle = *this;
		handle.OffsetInline(numDescriptors, descriptorSize);
		return handle;
	}

	void Reset()
	{
		CpuHandle = InvalidCPUHandle;
		GpuHandle = InvalidGPUHandle;
		HeapIndex = InvalidHeapIndex;
	}

	bool IsNull() const { return CpuHandle == InvalidCPUHandle; }
	bool IsShaderVisible() const { return GpuHandle != InvalidGPUHandle; }

	constexpr static D3D12_CPU_DESCRIPTOR_HANDLE InvalidCPUHandle = { ~0u };
	constexpr static D3D12_GPU_DESCRIPTOR_HANDLE InvalidGPUHandle = { ~0u };
	constexpr static int32 InvalidHeapIndex = -1;

	CD3DX12_CPU_DESCRIPTOR_HANDLE CpuHandle;
	CD3DX12_GPU_DESCRIPTOR_HANDLE GpuHandle;
	int32 HeapIndex;
};
