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

uint32 Buffer::GetSRVIndex() const
{
	return m_pSrv ? m_pSrv->GetHeapIndex() : DescriptorHandle::InvalidHeapIndex;
}

uint32 Buffer::GetUAVIndex() const
{
	return m_pUav ? m_pUav->GetHeapIndex() : DescriptorHandle::InvalidHeapIndex;
}
