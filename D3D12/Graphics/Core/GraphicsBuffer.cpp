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

D3D12_RESOURCE_DESC GetResourceDesc(const BufferDesc& bufferDesc)
{
	D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(
		Math::AlignUp<int64>((int64)bufferDesc.ElementSize * bufferDesc.ElementCount, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT),
		D3D12_RESOURCE_FLAG_NONE
	);
	if (EnumHasAnyFlags(bufferDesc.Usage, BufferFlag::ShaderResource | BufferFlag::AccelerationStructure) == false)
	{
		desc.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
	}
	if (EnumHasAnyFlags(bufferDesc.Usage, BufferFlag::UnorderedAccess))
	{
		desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	}
	return desc;
}

void Buffer::Create(const BufferDesc& bufferDesc)
{
	Release();
	m_Desc = bufferDesc;

	D3D12_RESOURCE_DESC desc = GetResourceDesc(bufferDesc);
	D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_DEFAULT;

	if (EnumHasAnyFlags(bufferDesc.Usage, BufferFlag::Readback))
	{
		SetResourceState(D3D12_RESOURCE_STATE_COPY_DEST);
		heapType = D3D12_HEAP_TYPE_READBACK;
	}
	else if (EnumHasAnyFlags(bufferDesc.Usage, BufferFlag::Upload))
	{
		heapType = D3D12_HEAP_TYPE_UPLOAD;
		SetResourceState(D3D12_RESOURCE_STATE_GENERIC_READ);
	}
	if (EnumHasAnyFlags(bufferDesc.Usage, BufferFlag::AccelerationStructure))
	{
		SetResourceState(D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
	}

	m_pResource = m_pGraphics->CreateResource(desc, GetResourceState(), heapType);

	SetName(m_Name.c_str());

	//#todo: Temp code. Pull out views from buffer
	if (EnumHasAnyFlags(bufferDesc.Usage, BufferFlag::UnorderedAccess))
	{
		if (EnumHasAnyFlags(bufferDesc.Usage, BufferFlag::Structured))
		{
			//Structured Buffer
			CreateUAV(&m_pUav, BufferUAVDesc(DXGI_FORMAT_UNKNOWN, false, true));
		}
		else if(EnumHasAnyFlags(bufferDesc.Usage, BufferFlag::ByteAddress))
		{
			//ByteAddress Buffer
			CreateUAV(&m_pUav, BufferUAVDesc(DXGI_FORMAT_UNKNOWN, true, false));
		}
		else
		{
			//Typed buffer
			CreateUAV(&m_pUav, BufferUAVDesc(bufferDesc.Format, false, false));
		}
	}
	if (EnumHasAnyFlags(bufferDesc.Usage, BufferFlag::ShaderResource | BufferFlag::AccelerationStructure))
	{
		if (EnumHasAnyFlags(bufferDesc.Usage, BufferFlag::Structured))
		{
			//Structured Buffer
			CreateSRV(&m_pSrv, BufferSRVDesc(DXGI_FORMAT_UNKNOWN, false));
		}
		else if (EnumHasAnyFlags(bufferDesc.Usage, BufferFlag::ByteAddress))
		{
			//ByteAddress Buffer
			CreateSRV(&m_pSrv, BufferSRVDesc(DXGI_FORMAT_UNKNOWN, true));
		}
		else
		{
			//Typed buffer
			CreateSRV(&m_pSrv, BufferSRVDesc(bufferDesc.Format));
		}
	}
}

void Buffer::SetData(CommandContext* pContext, const void* pData, uint64 dataSize, uint32 offset)
{
	check(dataSize + offset <= GetSize());
	pContext->InitializeBuffer(this, pData, dataSize, offset);
}

void* Buffer::Map(uint32 subResource /*= 0*/, uint64 readFrom /*= 0*/, uint64 readTo /*= 0*/)
{
	check(m_pResource);
	CD3DX12_RANGE range(readFrom, readTo);
	void* pMappedData = nullptr;
	m_pResource->Map(subResource, &range, &pMappedData);
	return pMappedData;
}

void Buffer::Unmap(uint32 subResource /*= 0*/, uint64 writtenFrom /*= 0*/, uint64 writtenTo /*= 0*/)
{
	check(m_pResource);
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