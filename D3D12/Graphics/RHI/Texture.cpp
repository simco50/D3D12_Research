#include "stdafx.h"
#include "Texture.h"
#include "Graphics.h"

Texture::Texture(GraphicsDevice* pParent, const TextureDesc& desc, ID3D12Resource* pResource)
	: GraphicsResource(pParent, pResource), m_Desc(desc)
{
}

Texture::~Texture()
{
	if (m_Rtv.ptr != 0)
	{
		bool isRTV = EnumHasAnyFlags(m_Desc.Usage, TextureFlag::RenderTarget);
		GetParent()->FreeDescriptor(isRTV ? D3D12_DESCRIPTOR_HEAP_TYPE_RTV : D3D12_DESCRIPTOR_HEAP_TYPE_DSV, m_Rtv);
		m_Rtv.ptr = 0;
	}
	if (m_ReadOnlyDsv.ptr != 0)
	{
		GetParent()->FreeDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, m_ReadOnlyDsv);
		m_ReadOnlyDsv.ptr = 0;
	}
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
