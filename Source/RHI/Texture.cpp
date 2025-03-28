#include "stdafx.h"
#include "Texture.h"
#include "Device.h"

Texture::Texture(GraphicsDevice* pParent, const TextureDesc& desc, ID3D12ResourceX* pResource)
	: DeviceResource(pParent, pResource), m_Desc(desc)
{
}

Texture::~Texture()
{
	GetParent()->ReleaseResourceDescriptor(m_SRV);
	for (DescriptorHandle& uav : m_UAVs)
		GetParent()->ReleaseResourceDescriptor(uav);
}
