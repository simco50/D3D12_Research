#include "stdafx.h"
#include "GraphicsResource.h"

GraphicsResource::GraphicsResource() 
	: m_pResource(nullptr), m_CurrentState(D3D12_RESOURCE_STATE_COMMON)
{
}

GraphicsResource::GraphicsResource(ID3D12Resource* pResource, D3D12_RESOURCE_STATES state) 
	: m_pResource(pResource), m_CurrentState(state)
{
}

GraphicsResource::~GraphicsResource()
{
	Release();
}

void GraphicsResource::Release()
{
	m_pResource.Reset();
}

void GraphicsResource::SetName(const char* pName)
{
	if (m_pResource)
	{
		wchar_t name[256];
		size_t written = 0;
		mbstowcs_s(&written, name, pName, 256);
		m_pResource->SetName(name);
	}
}