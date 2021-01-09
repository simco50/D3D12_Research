#include "stdafx.h"
#include "GraphicsResource.h"
#include "ResourceViews.h"

GraphicsResource::GraphicsResource(Graphics* pParent) 
	: GraphicsObject(pParent), m_pResource(nullptr), m_ResourceState(D3D12_RESOURCE_STATE_COMMON)
{
}

GraphicsResource::GraphicsResource(Graphics* pParent, ID3D12Resource* pResource, D3D12_RESOURCE_STATES state)
	: GraphicsObject(pParent), m_pResource(pResource), m_ResourceState(state)
{
}

GraphicsResource::~GraphicsResource()
{
	Release();
}

void* GraphicsResource::Map(uint32 subResource /*= 0*/, uint64 readFrom /*= 0*/, uint64 readTo /*= 0*/)
{
	check(m_pResource);
	check(m_pMappedData == nullptr);
	CD3DX12_RANGE range(readFrom, readTo);
	m_pResource->Map(subResource, &range, &m_pMappedData);
	return m_pMappedData;
}

void GraphicsResource::Unmap(uint32 subResource /*= 0*/, uint64 writtenFrom /*= 0*/, uint64 writtenTo /*= 0*/)
{
	check(m_pResource);
	CD3DX12_RANGE range(writtenFrom, writtenFrom);
	m_pResource->Unmap(subResource, &range);
	m_pMappedData = nullptr;
}

void GraphicsResource::Release()
{
	if (m_pResource)
	{
		m_pResource->Release();
		m_pResource = nullptr;
	}
}

void GraphicsResource::SetName(const char* pName)
{
	D3D::SetObjectName(m_pResource, pName);
}

std::string GraphicsResource::GetName() const
{
	if (m_pResource)
	{
		uint32 size = 0;
		m_pResource->GetPrivateData(WKPDID_D3DDebugObjectName, &size, nullptr);
		std::string str(size, '\0');
		m_pResource->GetPrivateData(WKPDID_D3DDebugObjectName, &size, &str[0]);
		return str;
	}
	return "";
}
