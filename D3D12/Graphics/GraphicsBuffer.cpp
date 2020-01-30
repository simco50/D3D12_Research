#include "stdafx.h"
#include "GraphicsBuffer.h"
#include "CommandContext.h"
#include "Graphics.h"
#include "Texture.h"

GraphicsBuffer::GraphicsBuffer(ID3D12Resource* pResource, D3D12_RESOURCE_STATES state)
	: GraphicsResource(pResource, state)
{

}

void GraphicsBuffer::Create(Graphics* pGraphics, uint64 elementCount, uint32 elementStride, bool cpuVisible)
{
	Release();

	m_ElementCount = elementCount;
	m_ElementStride = elementStride;
	m_CurrentState = cpuVisible ? D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_COMMON;

	D3D12_RESOURCE_DESC desc;
	desc.Alignment = 0;
	desc.DepthOrArraySize = 1;
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Flags = D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.Height = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.MipLevels = 1;
	desc.Width = GetSize();
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;

	m_pResource = pGraphics->CreateResource(desc, m_CurrentState, cpuVisible ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT);

	CreateViews(pGraphics);
}

void GraphicsBuffer::SetData(CommandContext* pContext, const void* pData, uint64 dataSize, uint32 offset)
{
	assert(dataSize + offset <= GetSize());
	pContext->InitializeBuffer(this, pData, dataSize);
}

void* GraphicsBuffer::Map(uint32 subResource /*= 0*/, uint64 readFrom /*= 0*/, uint64 readTo /*= 0*/)
{
	assert(m_pResource);
	CD3DX12_RANGE range(readFrom, readTo);
	void* pMappedData = nullptr;
	m_pResource->Map(subResource, &range, &pMappedData);
	return pMappedData;
}

void GraphicsBuffer::Unmap(uint32 subResource /*= 0*/, uint64 writtenFrom /*= 0*/, uint64 writtenTo /*= 0*/)
{
	assert(m_pResource);
	CD3DX12_RANGE range(writtenFrom, writtenFrom);
	m_pResource->Unmap(subResource, &range);
}

ByteAddressBuffer::ByteAddressBuffer(Graphics* pGraphics)
{
	m_Uav = pGraphics->AllocateCpuDescriptors(1, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	m_Srv = pGraphics->AllocateCpuDescriptors(1, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void ByteAddressBuffer::Create(Graphics* pGraphics, uint32 elementStride, uint64 elementCount, bool cpuVisible /*= false*/)
{
	m_ElementCount = elementCount;
	m_ElementStride = elementStride;
	m_CurrentState = cpuVisible ? D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_COMMON;

	D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(GetSize(), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	m_pResource = pGraphics->CreateResource(desc, m_CurrentState, cpuVisible ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT);

	CreateViews(pGraphics);
}

void ByteAddressBuffer::CreateViews(Graphics* pGraphics)
{
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
	uavDesc.Buffer.CounterOffsetInBytes = 0;
	uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
	uavDesc.Buffer.NumElements = (uint32)m_ElementCount;
	uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;

	pGraphics->GetDevice()->CreateUnorderedAccessView(m_pResource, nullptr, &uavDesc, m_Uav);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
	srvDesc.Buffer.NumElements = (uint32)m_ElementCount;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;

	pGraphics->GetDevice()->CreateShaderResourceView(m_pResource, &srvDesc, m_Srv);
}

StructuredBuffer::StructuredBuffer(Graphics* pGraphics)
{
	m_Uav = pGraphics->AllocateCpuDescriptors(1, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	m_Srv = pGraphics->AllocateCpuDescriptors(1, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void StructuredBuffer::Create(Graphics* pGraphics, uint32 elementStride, uint64 elementCount, bool cpuVisible /*= false*/)
{
	Release();

	m_ElementCount = elementCount;
	m_ElementStride = elementStride;
	m_CurrentState = cpuVisible ? D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_COMMON;

	D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(GetSize(), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	m_pResource = pGraphics->CreateResource(desc, m_CurrentState, cpuVisible ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT);

	CreateViews(pGraphics);
}

void StructuredBuffer::CreateViews(Graphics* pGraphics)
{
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
	uavDesc.Buffer.CounterOffsetInBytes = 0;
	uavDesc.Buffer.FirstElement = 0;
	uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
	uavDesc.Buffer.NumElements = (uint32)m_ElementCount;
	uavDesc.Buffer.StructureByteStride = m_ElementStride;
	uavDesc.Format = DXGI_FORMAT_UNKNOWN;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;

	if (m_pCounter == nullptr)
	{
		m_pCounter = std::make_unique<ByteAddressBuffer>(pGraphics);
		m_pCounter->Create(pGraphics, 4, 1, false);
	}

	pGraphics->GetDevice()->CreateUnorderedAccessView(m_pResource, m_pCounter->GetResource(), &uavDesc, m_Uav);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
	srvDesc.Buffer.NumElements = (uint32)m_ElementCount;
	srvDesc.Buffer.StructureByteStride = m_ElementStride;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;

	pGraphics->GetDevice()->CreateShaderResourceView(m_pResource, &srvDesc, m_Srv);
}