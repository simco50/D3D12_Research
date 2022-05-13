#include "stdafx.h"
#include "GraphicsResource.h"
#include "ResourceViews.h"
#include "Graphics.h"

GraphicsResource::GraphicsResource(GraphicsDevice* pParent, ID3D12Resource* pResource)
	: GraphicsObject(pParent), m_pResource(pResource)
{
}

GraphicsResource::~GraphicsResource()
{
	if (m_pResource)
	{
		if (m_ImmediateDelete)
		{
			m_pResource->Release();
		}
		else
		{
			GetParent()->DeferReleaseObject(m_pResource);
		}
		m_pResource = nullptr;
	}
}

void GraphicsResource::SetName(const char* pName)
{
	D3D::SetObjectName(m_pResource, pName);
	m_Name = pName;
}

int32 GraphicsResource::GetSRVIndex() const
{
	return m_pSrv ? m_pSrv->GetHeapIndex() : DescriptorHandle::InvalidHeapIndex;
}

int32 GraphicsResource::GetUAVIndex() const
{
	return m_pUav ? m_pUav->GetHeapIndex() : DescriptorHandle::InvalidHeapIndex;
}

