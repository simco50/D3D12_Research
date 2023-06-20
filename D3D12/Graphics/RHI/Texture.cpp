#include "stdafx.h"
#include "Texture.h"
#include "Graphics.h"

Texture::Texture(GraphicsDevice* pParent, const TextureDesc& desc, ID3D12Resource* pResource)
	: GraphicsResource(pParent, pResource), m_Desc(desc)
{
}

Texture::~Texture()
{
	if (m_RTV.ptr != 0)
		GetParent()->FreeCPUDescriptor(EnumHasAnyFlags(m_Desc.Usage, TextureFlag::RenderTarget) ? D3D12_DESCRIPTOR_HEAP_TYPE_RTV : D3D12_DESCRIPTOR_HEAP_TYPE_DSV, m_RTV);

	if (m_ReadOnlyDSV.ptr != 0)
		GetParent()->FreeCPUDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, m_ReadOnlyDSV);
}

D3D12_CPU_DESCRIPTOR_HANDLE Texture::GetDSV(bool writeable /*= true*/) const
{
	check(EnumHasAllFlags(m_Desc.Usage, TextureFlag::DepthStencil));
	return writeable ? m_RTV : m_ReadOnlyDSV;
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

D3D12_CPU_DESCRIPTOR_HANDLE Texture::GetRTV() const
{
	check(EnumHasAllFlags(m_Desc.Usage, TextureFlag::RenderTarget));
	return m_RTV;
}
