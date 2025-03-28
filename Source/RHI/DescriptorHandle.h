#pragma once

struct DescriptorPtr
{
	CD3DX12_CPU_DESCRIPTOR_HANDLE CPUHandle;
	CD3DX12_GPU_DESCRIPTOR_HANDLE GPUHandle;
	CD3DX12_CPU_DESCRIPTOR_HANDLE CPUOpaqueHandle;
	uint32 HeapIndex;

	DescriptorPtr Offset(uint32 i, uint32 descriptorSize) const
	{
		DescriptorPtr h = *this;
		h.CPUHandle.Offset(i, descriptorSize);
		h.GPUHandle.Offset(i, descriptorSize);
		h.CPUOpaqueHandle.Offset(i, descriptorSize);
		h.HeapIndex += i;
		return h;
	}
};

class DescriptorHandle
{
public:
	constexpr DescriptorHandle()
		: HeapIndex(InvalidHeapIndex)
	{}

	explicit constexpr DescriptorHandle(uint32 index)
		: HeapIndex(index)
	{}

	void Reset() { HeapIndex = InvalidHeapIndex; }

	bool IsValid() const { return HeapIndex != InvalidHeapIndex; }

	operator uint32() const { return HeapIndex;	}

	constexpr static uint32 InvalidHeapIndex = 0xFFFFFFFF;
	uint32 HeapIndex;
};

template<bool Writeable>
class DescriptorHandleT : public DescriptorHandle
{
public:
	constexpr DescriptorHandleT() = default;

	explicit constexpr DescriptorHandleT(uint32 index)
		: DescriptorHandle(index)
	{
	}

	explicit constexpr DescriptorHandleT(const DescriptorPtr& inPtr)
		: DescriptorHandle(inPtr.HeapIndex)
	{
	}

	static constexpr DescriptorHandleT Invalid() { return DescriptorHandleT(); }

	static constexpr bool IsWriteable = Writeable;
};

class Texture;
class Buffer;

using SRVHandle = DescriptorHandleT<false>;
using UAVHandle = DescriptorHandleT<true>;
