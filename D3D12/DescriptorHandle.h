#pragma once

class DescriptorHandle
{
public:
	DescriptorHandle()
	{
		m_CpuHandle.ptr = InvalidHandle;
		m_GpuHandle.ptr = InvalidHandle;
	}

	DescriptorHandle(D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle)
		: m_CpuHandle(CpuHandle)
	{
		m_GpuHandle.ptr = InvalidHandle;
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
		if (m_CpuHandle.ptr != InvalidHandle)
		{
			m_CpuHandle.ptr += offsetScaledByDescriptorSize;
		}
		if (m_GpuHandle.ptr != InvalidHandle)
		{
			m_GpuHandle.ptr += offsetScaledByDescriptorSize;
		}
	}

	D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle() const { return m_CpuHandle; }
	D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle() const { return m_GpuHandle; }

	bool IsNull() const { return m_CpuHandle.ptr == InvalidHandle; }
	bool IsShaderVisible() const { return m_GpuHandle.ptr != InvalidHandle; }

	constexpr static size_t InvalidHandle = ~(size_t)0;

private:
	D3D12_CPU_DESCRIPTOR_HANDLE m_CpuHandle;
	D3D12_GPU_DESCRIPTOR_HANDLE m_GpuHandle;
};