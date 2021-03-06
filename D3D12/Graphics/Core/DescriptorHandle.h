#pragma once

class DescriptorHandle
{
public:
	DescriptorHandle()
	{
		CpuHandle = InvalidCPUHandle;
		GpuHandle = InvalidGPUHandle;
	}

	DescriptorHandle(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = InvalidGPUHandle, uint32 heapIndex = ~0u)
		: CpuHandle(cpuHandle), GpuHandle(gpuHandle), HeapIndex(heapIndex)
	{
	}

	void Offset(uint32 numDescriptors, uint32 descriptorSize)
	{
		if (CpuHandle != InvalidCPUHandle)
		{
			CpuHandle.Offset(numDescriptors, descriptorSize);
		}
		if (GpuHandle != InvalidGPUHandle)
		{
			GpuHandle.Offset(numDescriptors, descriptorSize);
		}
		if (HeapIndex != ~0u)
		{
			HeapIndex += numDescriptors;
		}
	}

	bool IsNull() const { return CpuHandle == InvalidCPUHandle; }
	bool IsShaderVisible() const { return GpuHandle != InvalidGPUHandle; }

	constexpr static D3D12_CPU_DESCRIPTOR_HANDLE InvalidCPUHandle = { ~0u };
	constexpr static D3D12_GPU_DESCRIPTOR_HANDLE InvalidGPUHandle = { ~0u };

	CD3DX12_CPU_DESCRIPTOR_HANDLE CpuHandle;
	CD3DX12_GPU_DESCRIPTOR_HANDLE GpuHandle;
	uint32 HeapIndex = ~0u;
};
