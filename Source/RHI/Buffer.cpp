#include "stdafx.h"
#include "Buffer.h"
#include "Device.h"

Buffer::Buffer(GraphicsDevice* pParent, const BufferDesc& desc, ID3D12ResourceX* pResource)
	: DeviceResource(pParent, pResource), m_Desc(desc)
{
}


Buffer::~Buffer()
{
	GetParent()->ReleaseResourceDescriptor(m_SRV);
	GetParent()->ReleaseResourceDescriptor(m_UAV);
}
