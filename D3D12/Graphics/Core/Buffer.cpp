#include "stdafx.h"
#include "Buffer.h"
#include "CommandContext.h"
#include "Graphics.h"
#include "ResourceViews.h"

Buffer::Buffer(GraphicsDevice* pParent, const char* pName /*= ""*/)
	: GraphicsResource(pParent)
{
	m_Name = pName;
}

Buffer::Buffer(GraphicsDevice* pParent, ID3D12Resource* pResource, D3D12_RESOURCE_STATES state)
	: GraphicsResource(pParent, pResource, state)
{

}

Buffer::Buffer(GraphicsDevice* pParent, const BufferDesc& desc, const char* pName /*= ""*/)
	: Buffer(pParent, pName)
{
	Create(desc);
}

Buffer::~Buffer()
{
}

D3D12_RESOURCE_DESC GetResourceDesc(const BufferDesc& bufferDesc)
{
	D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(
		Math::AlignUp<uint64>(bufferDesc.Size, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT),
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
	//PIX: This will improve the shaders' performance on some hardware.
	if (EnumHasAnyFlags(bufferDesc.Usage, BufferFlag::Structured))
	{
		desc.Width = Math::Max(desc.Width, 16ull);
	}
	return desc;
}

void Buffer::Create(const BufferDesc& bufferDesc)
{
	Release();
	m_Desc = bufferDesc;

	D3D12_RESOURCE_DESC desc = GetResourceDesc(bufferDesc);
	D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_UNKNOWN;

	if (EnumHasAnyFlags(bufferDesc.Usage, BufferFlag::Readback))
	{
		check(initialState == D3D12_RESOURCE_STATE_UNKNOWN);
		initialState = D3D12_RESOURCE_STATE_COPY_DEST;
		heapType = D3D12_HEAP_TYPE_READBACK;
	}
	if (EnumHasAnyFlags(bufferDesc.Usage, BufferFlag::Upload))
	{
		check(initialState == D3D12_RESOURCE_STATE_UNKNOWN);
		initialState = D3D12_RESOURCE_STATE_GENERIC_READ;
		heapType = D3D12_HEAP_TYPE_UPLOAD;
	}
	if (EnumHasAnyFlags(bufferDesc.Usage, BufferFlag::AccelerationStructure))
	{
		check(initialState == D3D12_RESOURCE_STATE_UNKNOWN);
		initialState = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
	}

	if (initialState == D3D12_RESOURCE_STATE_UNKNOWN)
	{
		initialState = D3D12_RESOURCE_STATE_COMMON;
	}

	m_pResource = GetParent()->CreateResource(desc, initialState, heapType);
	SetResourceState(initialState);

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

void Buffer::SetData(CommandContext* pContext, const void* pData, uint64 dataSize, uint64 offset)
{
	check(dataSize + offset <= GetSize());
	pContext->InitializeBuffer(this, pData, dataSize, offset);
}

void Buffer::CreateUAV(UnorderedAccessView** pView, const BufferUAVDesc& desc)
{
	if (*pView == nullptr)
	{
		m_Descriptors.push_back(std::make_unique<UnorderedAccessView>());
		*pView = static_cast<UnorderedAccessView*>(m_Descriptors.back().get());
	}
	(*pView)->Create(this, desc);
}

void Buffer::CreateSRV(ShaderResourceView** pView, const BufferSRVDesc& desc)
{
	if (*pView == nullptr)
	{
		m_Descriptors.push_back(std::make_unique<ShaderResourceView>());
		*pView = static_cast<ShaderResourceView*>(m_Descriptors.back().get());
	}
	(*pView)->Create(this, desc);
}
