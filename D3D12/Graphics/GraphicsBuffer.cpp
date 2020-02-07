#include "stdafx.h"
#include "GraphicsBuffer.h"
#include "CommandContext.h"
#include "Graphics.h"
#include "ResourceViews.h"

Buffer::Buffer(ID3D12Resource* pResource, D3D12_RESOURCE_STATES state)
	: GraphicsResource(pResource, state), m_pName(nullptr)
{}

Buffer::Buffer(const char* pName)
	: m_pName(pName)
{

}

Buffer::~Buffer()
{

}

void Buffer::Create(Graphics* pGraphics, const BufferDesc& bufferDesc)
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
	desc.Width = (int64)bufferDesc.ElementSize * bufferDesc.ElementCount;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;

	D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_DEFAULT;
	if (Any(bufferDesc.Usage, BufferFlag::ShaderResource) == false)
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
	m_pResource = pGraphics->CreateResource(desc, m_CurrentState, heapType);

	if (m_pName)
	{
		SetName(m_pName);
	}
}

void Buffer::SetData(CommandContext* pContext, const void* pData, uint64 dataSize, uint32 offset)
{
	assert(dataSize + offset <= GetSize());
	pContext->InitializeBuffer(this, pData, dataSize);
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

void ByteAddressBuffer::Create(Graphics* pGraphics, uint32 elementStride, uint32 elementCount, bool cpuVisible /*= false*/)
{
	BufferFlag flags = BufferFlag::ShaderResource | BufferFlag::UnorderedAccess;
	if (cpuVisible)
	{
		flags |= BufferFlag::Upload;
	}
	Buffer::Create(pGraphics, BufferDesc::CreateByteAddress(elementCount * elementStride, flags));

	m_Srv.Create(pGraphics, this, BufferSRVDesc::CreateByteAddress());
	m_Uav.Create(pGraphics, this, BufferUAVDesc::CreateByteAddress());
}

void StructuredBuffer::Create(Graphics* pGraphics, uint32 elementStride, uint32 elementCount, bool cpuVisible /*= false*/)
{
	BufferFlag flags = BufferFlag::ShaderResource | BufferFlag::UnorderedAccess;
	if (cpuVisible)
	{
		flags |= BufferFlag::Upload;
	}
	Buffer::Create(pGraphics, BufferDesc::CreateStructured(elementCount, elementStride, flags));

	if (m_pCounter == nullptr)
	{
		m_pCounter = std::make_unique<ByteAddressBuffer>();
		m_pCounter->Create(pGraphics, 4, 1, false);
	}

	m_Srv.Create(pGraphics, this, BufferSRVDesc::CreateStructured());
	m_Uav.Create(pGraphics, this, BufferUAVDesc::CreateStructured(m_pCounter.get()));
}

D3D12_CPU_DESCRIPTOR_HANDLE BufferWithDescriptors::GetSRV() const
{
	return m_Srv.GetDescriptor();
}

D3D12_CPU_DESCRIPTOR_HANDLE BufferWithDescriptors::GetUAV() const
{
	return m_Uav.GetDescriptor();
}
