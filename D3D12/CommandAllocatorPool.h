#pragma once
class CommandAllocatorPool
{
public:
	CommandAllocatorPool(ID3D12Device* pDevice, D3D12_COMMAND_LIST_TYPE type);
	~CommandAllocatorPool();

	ID3D12CommandAllocator* GetAllocator(__int64 fenceValue);
	void FreeAllocator(ID3D12CommandAllocator* pAllocator, __int64 fenceValue);

private:
	std::vector<ComPtr<ID3D12CommandAllocator>> m_CommandAllocators;
	std::queue<std::pair<ID3D12CommandAllocator*, __int64>> m_FreeAllocators;

	ID3D12Device* m_pDevice;
	D3D12_COMMAND_LIST_TYPE m_Type;
};

