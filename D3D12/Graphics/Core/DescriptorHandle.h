#pragma once

class DescriptorHandle
{
public:
	DescriptorHandle()
	{
		m_CpuHandle = InvalidCPUHandle;
		m_GpuHandle = InvalidGPUHandle;
	}

	DescriptorHandle(D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle)
		: m_CpuHandle(CpuHandle), m_GpuHandle(InvalidGPUHandle)
	{
	}

	DescriptorHandle(D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle)
		: m_CpuHandle(CpuHandle), m_GpuHandle(GpuHandle)
	{
	}

	DescriptorHandle operator+ (uint32 offsetScaledByDescriptorSize) const
	{
		DescriptorHandle ret = *this;
		ret += offsetScaledByDescriptorSize;
		return ret;
	}

	void operator += (uint32 offsetScaledByDescriptorSize)
	{
		if (m_CpuHandle != InvalidCPUHandle)
		{
			m_CpuHandle.Offset(1, offsetScaledByDescriptorSize);
		}
		if (m_GpuHandle != InvalidGPUHandle)
		{
			m_GpuHandle.Offset(1, offsetScaledByDescriptorSize);
		}
	}

	D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle() const { return m_CpuHandle; }
	D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle() const { return m_GpuHandle; }

	bool IsNull() const { return m_CpuHandle == InvalidCPUHandle; }
	bool IsShaderVisible() const { return m_GpuHandle != InvalidGPUHandle; }

	constexpr static D3D12_CPU_DESCRIPTOR_HANDLE InvalidCPUHandle = { ~0u };
	constexpr static D3D12_GPU_DESCRIPTOR_HANDLE InvalidGPUHandle = { ~0u };

private:
	CD3DX12_CPU_DESCRIPTOR_HANDLE m_CpuHandle;
	CD3DX12_GPU_DESCRIPTOR_HANDLE m_GpuHandle;
};
