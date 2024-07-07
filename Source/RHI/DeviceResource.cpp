#include "stdafx.h"
#include "DeviceResource.h"
#include "ResourceViews.h"
#include "Device.h"

DeviceResource::DeviceResource(GraphicsDevice* pParent, ID3D12Resource* pResource)
	: DeviceObject(pParent), m_pResource(pResource)
{
}

DeviceResource::~DeviceResource()
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

void DeviceResource::SetName(const char* pName)
{
	D3D::SetObjectName(m_pResource, pName);
	m_Name = pName;
}
