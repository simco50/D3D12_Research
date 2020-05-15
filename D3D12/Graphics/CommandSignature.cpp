#include "stdafx.h"
#include "CommandSignature.h"

CommandSignature::CommandSignature()
{
}

CommandSignature::~CommandSignature()
{
}

void CommandSignature::Finalize(const char* pName, ID3D12Device* pDevice)
{
	D3D12_COMMAND_SIGNATURE_DESC desc{};
	desc.ByteStride = m_Stride;
	desc.NodeMask = 0;
	desc.NumArgumentDescs = m_ArgumentDesc.size();
	desc.pArgumentDescs = m_ArgumentDesc.data();
	HR(pDevice->CreateCommandSignature(&desc, m_pRootSignature, IID_PPV_ARGS(m_pCommandSignature.GetAddressOf())));
	SetD3DObjectName(m_pCommandSignature.Get(), pName);
}

void CommandSignature::AddDispatch()
{
	D3D12_INDIRECT_ARGUMENT_DESC desc;
	desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
	m_ArgumentDesc.push_back(desc);
	m_Stride += 3 * sizeof(uint32);
}

void CommandSignature::AddDraw()
{
	D3D12_INDIRECT_ARGUMENT_DESC desc;
	desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
	m_ArgumentDesc.push_back(desc);
	m_Stride += 4 * sizeof(uint32);
}

