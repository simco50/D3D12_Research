#include "stdafx.h"
#include "GraphicsBuffer.h"
#include "CommandContext.h"
#include "Graphics.h"
#include "ResourceViews.h"

Buffer::Buffer(Graphics* pGraphics, const char* pName /*= ""*/)
	: GraphicsResource(pGraphics), m_Name(pName)
{
}

Buffer::Buffer(Graphics* pGraphics, ID3D12Resource* pResource, D3D12_RESOURCE_STATES state)
	: GraphicsResource(pGraphics, pResource, state)
{

}

Buffer::~Buffer()
{

}

void Buffer::Create(const BufferDesc& bufferDesc)
{
	Release();
	m_Desc = bufferDesc;

	D3D12_RESOURCE_DESC desc;
	desc.Alignment = 0;
	desc.DepthOrArraySize = 1;
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Flags = D3D12_RESOURCE_FLAG_NONE;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.Height = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.MipLevels = 1;
	desc.Width = Math::AlignUp<int64>((int64)bufferDesc.ElementSize * bufferDesc.ElementCount, 16);
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;

	D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_DEFAULT;
	if (Any(bufferDesc.Usage, BufferFlag::ShaderResource | BufferFlag::AccelerationStructure) == false)
	{
		desc.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
	}
	if (Any(bufferDesc.Usage, BufferFlag::UnorderedAccess))
	{
		desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	}

	if (Any(bufferDesc.Usage, BufferFlag::Readback))
	{
		m_CurrentState = D3D12_RESOURCE_STATE_COPY_DEST;
		heapType = D3D12_HEAP_TYPE_READBACK;
	}
	else if (Any(bufferDesc.Usage, BufferFlag::Upload))
	{
		heapType = D3D12_HEAP_TYPE_UPLOAD;
		m_CurrentState = D3D12_RESOURCE_STATE_GENERIC_READ;
	}
	if (Any(bufferDesc.Usage, BufferFlag::AccelerationStructure))
	{
		m_CurrentState = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
	}

	m_pResource = m_pGraphics->CreateResource(desc, m_CurrentState, heapType);

	SetName(m_Name.c_str());

	//#todo: Temp code. Pull out views from buffer
	if (Any(bufferDesc.Usage, BufferFlag::UnorderedAccess))
	{
		if (Any(bufferDesc.Usage, BufferFlag::Structured))
		{
			CreateUAV(&m_pUav, BufferUAVDesc(DXGI_FORMAT_UNKNOWN, false, true));
		}
		else
		{
			CreateUAV(&m_pUav, BufferUAVDesc(DXGI_FORMAT_UNKNOWN, true, false));
		}
	}
	if (Any(bufferDesc.Usage, BufferFlag::ShaderResource | BufferFlag::AccelerationStructure))
	{
		CreateSRV(&m_pSrv, BufferSRVDesc(DXGI_FORMAT_UNKNOWN));
	}
}

void Buffer::SetData(CommandContext* pContext, const void* pData, uint64 dataSize, uint32 offset)
{
	assert(dataSize + offset <= GetSize());
	pContext->InitializeBuffer(this, pData, dataSize, offset);
}

void* Buffer::Map(uint32 subResource /*= 0*/, uint64 readFrom /*= 0*/, uint64 readTo /*= 0*/)
{
	assert(m_pResource);
	CD3DX12_RANGE range(readFrom, readTo);
	void* pMappedData = nullptr;
	m_pResource->Map(subResource, &range, &pMappedData);
	return pMappedData;
}

void Buffer::Unmap(uint32 subResource /*= 0*/, uint64 writtenFrom /*= 0*/, uint64 writtenTo /*= 0*/)
{
	assert(m_pResource);
	CD3DX12_RANGE range(writtenFrom, writtenFrom);
	m_pResource->Unmap(subResource, &range);
}

void Buffer::CreateUAV(UnorderedAccessView** pView, const BufferUAVDesc& desc)
{
	if (*pView == nullptr)
	{
		m_Descriptors.push_back(std::make_unique<UnorderedAccessView>(m_pGraphics));
		*pView = static_cast<UnorderedAccessView*>(m_Descriptors.back().get());
	}
	(*pView)->Create(this, desc);
}

void Buffer::CreateSRV(ShaderResourceView** pView, const BufferSRVDesc& desc)
{
	if (*pView == nullptr)
	{
		m_Descriptors.push_back(std::make_unique<ShaderResourceView>(m_pGraphics));
		*pView = static_cast<ShaderResourceView*>(m_Descriptors.back().get());
	}
	(*pView)->Create(this, desc);
}