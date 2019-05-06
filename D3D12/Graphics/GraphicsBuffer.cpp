#include "stdafx.h"
#include "GraphicsBuffer.h"
#include "CommandContext.h"
#include "Graphics.h"

void GraphicsBuffer::Create(ID3D12Device* pDevice, uint32 size, bool cpuVisible)
{
	m_Size = size;
	D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(size, D3D12_RESOURCE_FLAG_NONE, 0);
	D3D12_HEAP_PROPERTIES properties = CD3DX12_HEAP_PROPERTIES(cpuVisible ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT);
	HR(pDevice->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&m_pResource)));
	m_CurrentState = D3D12_RESOURCE_STATE_COMMON;
}

void GraphicsBuffer::SetData(CommandContext* pContext, void* pData, uint32 dataSize, uint32 offset)
{
	assert(dataSize + offset <= m_Size);
	pContext->InitializeBuffer(this, pData, dataSize);
}

void StructuredBuffer::Create(Graphics* pGraphics, uint32 elementStride, uint32 elementCount, bool cpuVisible /*= false*/)
{
	Release();

	m_Size = elementCount * elementStride;
	const int alignment = 16;
	int bufferSize = (m_Size + (alignment - 1)) & ~(alignment - 1);

	m_Size = elementCount * elementStride;
	D3D12_RESOURCE_DESC desc = {};
	desc.Alignment = 0;
	desc.DepthOrArraySize = 1;
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.Height = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Width = bufferSize;

	D3D12_HEAP_PROPERTIES properties = CD3DX12_HEAP_PROPERTIES(cpuVisible ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT);
	HR(pGraphics->GetDevice()->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_pResource)));
	m_CurrentState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
	uavDesc.Buffer.CounterOffsetInBytes = 0;
	uavDesc.Buffer.FirstElement = 0;
	uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
	uavDesc.Buffer.NumElements = elementCount;
	uavDesc.Buffer.StructureByteStride = elementStride;
	uavDesc.Format = DXGI_FORMAT_UNKNOWN;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;

	if (m_Uav.ptr == 0)
	{
		m_Uav = pGraphics->AllocateCpuDescriptors(1, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}
	pGraphics->GetDevice()->CreateUnorderedAccessView(m_pResource.Get(), nullptr, &uavDesc, m_Uav);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
	srvDesc.Buffer.NumElements = elementCount;
	srvDesc.Buffer.StructureByteStride = elementStride;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;

	if (m_Srv.ptr == 0)
	{
		m_Srv = pGraphics->AllocateCpuDescriptors(1, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}
	pGraphics->GetDevice()->CreateShaderResourceView(m_pResource.Get(), &srvDesc, m_Srv);
}