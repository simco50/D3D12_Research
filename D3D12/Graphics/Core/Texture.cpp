#include "stdafx.h"
#include "Texture.h"

Texture::Texture(GraphicsDevice* pParent, const TextureDesc& desc, ID3D12Resource* pResource)
	: GraphicsResource(pParent, pResource), m_Desc(desc)
{
}

D3D12_CPU_DESCRIPTOR_HANDLE Texture::GetDSV(bool writeable /*= true*/) const
{
	check(EnumHasAllFlags(m_Desc.Usage, TextureFlag::DepthStencil));
	return writeable ? m_Rtv : m_ReadOnlyDsv;
}

D3D12_CPU_DESCRIPTOR_HANDLE Texture::GetRTV() const
{
	check(EnumHasAllFlags(m_Desc.Usage, TextureFlag::RenderTarget));
	return m_Rtv;
}
