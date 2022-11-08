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
		GetParent()->FreeCPUDescriptor(EnumHasAnyFlags(m_Desc.Usage, TextureFlag::RenderTarget) ? D3D12_DESCRIPTOR_HEAP_TYPE_RTV : D3D12_DESCRIPTOR_HEAP_TYPE_DSV, m_Rtv);

	if (m_ReadOnlyDsv.ptr != 0)
		GetParent()->FreeCPUDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, m_ReadOnlyDsv);
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

UnorderedAccessView* Texture::GetSubResourceUAV(uint32 subresourceIndex)
{
	if (m_SubresourceUAVs.empty())
	{
		m_SubresourceUAVs.resize(m_Desc.Mips);
	}
	if (!m_SubresourceUAVs[subresourceIndex])
	{
		m_SubresourceUAVs[subresourceIndex] = GetParent()->CreateUAV(this, TextureUAVDesc((uint8)subresourceIndex));
	}
	return m_SubresourceUAVs[subresourceIndex];
}
