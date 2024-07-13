#include "stdafx.h"
#include "Texture.h"
#include "Device.h"

Texture::Texture(GraphicsDevice* pParent, const TextureDesc& desc, ID3D12Resource* pResource)
	: DeviceResource(pParent, pResource), m_Desc(desc)
{
}

Texture::~Texture()
{
}

UnorderedAccessView* Texture::GetUAV(uint32 subresourceIndex) const
{
	gAssert(subresourceIndex < m_UAVs.size());
	return m_UAVs[subresourceIndex];
}

uint32 Texture::GetUAVIndex(uint32 subresourceIndex) const
{
	return GetUAV(subresourceIndex)->GetHeapIndex();
}

uint32 Texture::GetSRVIndex() const
{
	return m_pSRV ? m_pSRV->GetHeapIndex() : DescriptorHandle::InvalidHeapIndex;
}

