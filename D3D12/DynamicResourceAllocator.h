#pragma once
struct DynamicAllocation
{
	ID3D12Resource* pBackingResource = nullptr;
	D3D12_GPU_VIRTUAL_ADDRESS GpuHandle{0};
	int Offset = 0;
	int Size = 0;
	void* pMappedMemory = nullptr;
};

class DynamicResourceAllocator
{
public:
	DynamicResourceAllocator(ID3D12Device* pDevice, bool gpuVisible, int size);
	~DynamicResourceAllocator();

	DynamicAllocation Allocate(int size);
	void Free(int fenceValue);

private:
	ComPtr<ID3D12Resource> m_pBackingResource;
	std::queue<std::pair<uint64, uint32>> m_FenceOffsets;
	int m_CurrentOffset = 0;
	int m_Size = 0;
	void* m_pMappedMemory = nullptr;
};