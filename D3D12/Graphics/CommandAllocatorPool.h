#pragma once
#include "GraphicsResource.h"

class CommandAllocatorPool : public GraphicsObject
{
public:
	CommandAllocatorPool(Graphics* pGraphics, D3D12_COMMAND_LIST_TYPE type);
	~CommandAllocatorPool();

	ID3D12CommandAllocator* GetAllocator(uint64 fenceValue);
	void FreeAllocator(ID3D12CommandAllocator* pAllocator, uint64 fenceValue);

private:
	std::vector<ComPtr<ID3D12CommandAllocator>> m_CommandAllocators;
	std::queue<std::pair<ID3D12CommandAllocator*, uint64>> m_FreeAllocators;

	D3D12_COMMAND_LIST_TYPE m_Type;
};

