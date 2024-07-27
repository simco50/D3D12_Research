#include "stdafx.h"
#include "Buffer.h"
#include "ResourceViews.h"

Buffer::Buffer(GraphicsDevice* pParent, const BufferDesc& desc, ID3D12ResourceX* pResource)
	: DeviceResource(pParent, pResource), m_Desc(desc)
{
}

uint32 Buffer::GetUAVIndex() const
{
	return m_pUAV->GetHeapIndex();
}

uint32 Buffer::GetSRVIndex() const
{
	return m_pSRV->GetHeapIndex();
}

