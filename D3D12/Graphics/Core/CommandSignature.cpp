#include "stdafx.h"
#include "CommandSignature.h"

void CommandSignature::Finalize(const char* pName, ID3D12Device* pDevice)
{
	D3D12_COMMAND_SIGNATURE_DESC desc{};
	desc.ByteStride = m_Stride;
	desc.NodeMask = 0;
	desc.NumArgumentDescs = (uint32)m_ArgumentDesc.size();
	desc.pArgumentDescs = m_ArgumentDesc.data();
	VERIFY_HR_EX(pDevice->CreateCommandSignature(&desc, m_pRootSignature, IID_PPV_ARGS(m_pCommandSignature.GetAddressOf())), pDevice);
	D3D::SetObjectName(m_pCommandSignature.Get(), pName);
}

void CommandSignature::AddDispatch()
{
	D3D12_INDIRECT_ARGUMENT_DESC desc;
	desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
	m_ArgumentDesc.push_back(desc);
	constexpr int dispatchArgumentsCount = 3;
	m_Stride += dispatchArgumentsCount * sizeof(uint32);
}

void CommandSignature::AddDraw()
{
	D3D12_INDIRECT_ARGUMENT_DESC desc;
	desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
	m_ArgumentDesc.push_back(desc);
	constexpr int drawArgumentsCount = 4;
	m_Stride += drawArgumentsCount * sizeof(uint32);
}

void CommandSignature::AddDrawIndexed()
{
	D3D12_INDIRECT_ARGUMENT_DESC desc;
	desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
	m_ArgumentDesc.push_back(desc);
	constexpr int drawArgumentsCount = 4;
	m_Stride += drawArgumentsCount * sizeof(uint32);
}