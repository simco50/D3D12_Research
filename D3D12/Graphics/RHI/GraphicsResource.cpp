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

void GraphicsResource::GetAllocationInfo(uint64& outSize, uint64& outAlignment) const
{
	D3D12_RESOURCE_DESC desc = m_pResource->GetDesc();
	D3D12_RESOURCE_ALLOCATION_INFO info = GetParent()->GetDevice()->GetResourceAllocationInfo(0, 1, &desc);
	outSize = info.SizeInBytes;
	outAlignment = info.Alignment;
}
