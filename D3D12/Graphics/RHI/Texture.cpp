#include "stdafx.h"
#include "Texture.h"
#include "Graphics.h"

Texture::Texture(GraphicsDevice* pParent, const TextureDesc& desc, ID3D12Resource* pResource)
	: GraphicsResource(pParent, pResource), m_Desc(desc)
{
}

Texture::~Texture()
{
}

UnorderedAccessView* Texture::GetUAV(uint32 subresourceIndex) const
{
	check(subresourceIndex < m_UAVs.size());
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

