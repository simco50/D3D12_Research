#include "stdafx.h"
#include "CommandAllocatorPool.h"

CommandAllocatorPool::CommandAllocatorPool(ID3D12Device* pDevice, D3D12_COMMAND_LIST_TYPE type)
{
	m_pDevice = pDevice;
	m_Type = type;
}

CommandAllocatorPool::~CommandAllocatorPool()
{

}

ID3D12CommandAllocator* CommandAllocatorPool::GetAllocator(__int64 fenceValue)
{
	if (m_FreeAllocators.empty() == false)
	{
		std::pair<ID3D12CommandAllocator*, __int64>& pFirst = m_FreeAllocators.front();
		if (pFirst.second <= fenceValue)
		{
			m_FreeAllocators.pop();
			pFirst.first->Reset();
			return pFirst.first;
		}
	}

	ComPtr<ID3D12CommandAllocator> pAllocator;
	m_pDevice->CreateCommandAllocator(m_Type, IID_PPV_ARGS(pAllocator.GetAddressOf()));
	m_CommandAllocators.push_back(std::move(pAllocator));
	return m_CommandAllocators.back().Get();
}

void CommandAllocatorPool::FreeAllocator(ID3D12CommandAllocator* pAllocator, __int64 fenceValue)
{
	m_FreeAllocators.push(std::pair<ID3D12CommandAllocator*, __int64>(pAllocator, fenceValue));
}

