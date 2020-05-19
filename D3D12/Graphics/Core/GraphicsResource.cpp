#include "stdafx.h"
#include "GraphicsResource.h"

GraphicsResource::GraphicsResource(Graphics* pParent) 
	: GraphicsObject(pParent), m_pResource(nullptr), m_CurrentState(D3D12_RESOURCE_STATE_COMMON)
{
}

GraphicsResource::GraphicsResource(Graphics* pParent, ID3D12Resource* pResource, D3D12_RESOURCE_STATES state)
	: GraphicsObject(pParent), m_pResource(pResource), m_CurrentState(state)
{
}

GraphicsResource::~GraphicsResource()
{
	Release();
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
	SetD3DObjectName(m_pResource, pName);
}

std::string GraphicsResource::GetName() const
{
	if (m_pResource)
	{
		uint32 size = 0;
		m_pResource->GetPrivateData(WKPDID_D3DDebugObjectName, &size, nullptr);
		std::string str(size, '\0');
		m_pResource->GetPrivateData(WKPDID_D3DDebugObjectName, &size, str.data());
		return str;
	}
	return "";
}
