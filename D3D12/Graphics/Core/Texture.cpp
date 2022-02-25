#include "stdafx.h"
#include "Texture.h"
#include "Content/Image.h"
#include "CommandContext.h"
#include "Graphics.h"
#include "OfflineDescriptorAllocator.h"
#include "ResourceViews.h"

Texture::Texture(GraphicsDevice* pParent, const char* pName)
	: GraphicsResource(pParent)
{
	m_Name = pName;
}

D3D12_CPU_DESCRIPTOR_HANDLE Texture::GetDSV(bool writeable /*= true*/) const
{
	check(EnumHasAllFlags(m_Desc.Usage, TextureFlag::DepthStencil));
	return writeable ? m_Rtv : m_ReadOnlyDsv;
}

int32 Texture::GetSRVIndex() const
{
	return m_pSrv ? m_pSrv->GetHeapIndex() : DescriptorHandle::InvalidHeapIndex;
}

int32 Texture::GetUAVIndex() const
{
	return m_pUav ? m_pUav->GetHeapIndex() : DescriptorHandle::InvalidHeapIndex;
}

void Texture::CreateUAV(UnorderedAccessView** pView, const TextureUAVDesc& desc)
{
	if (*pView == nullptr)
	{
		m_Descriptors.push_back(std::make_unique<UnorderedAccessView>());
		*pView = static_cast<UnorderedAccessView*>(m_Descriptors.back().get());
	}
	(*pView)->Create(this, desc);
}

void Texture::CreateSRV(ShaderResourceView** pView, const TextureSRVDesc& desc)
{
	if (*pView == nullptr)
	{
		m_Descriptors.push_back(std::make_unique<ShaderResourceView>());
		*pView = static_cast<ShaderResourceView*>(m_Descriptors.back().get());
	}
	(*pView)->Create(this, desc);
}

D3D12_CPU_DESCRIPTOR_HANDLE Texture::GetRTV() const
{
	check(EnumHasAllFlags(m_Desc.Usage, TextureFlag::RenderTarget));
	return m_Rtv;
}

DXGI_FORMAT Texture::GetSrvFormat(DXGI_FORMAT format)
{
	switch (format)
	{
		// 32-bit Z w/ Stencil
	case DXGI_FORMAT_R32G8X24_TYPELESS:
	case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
	case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
	case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
		return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;

		// No Stencil
	case DXGI_FORMAT_R32_TYPELESS:
	case DXGI_FORMAT_D32_FLOAT:
	case DXGI_FORMAT_R32_FLOAT:
		return DXGI_FORMAT_R32_FLOAT;

		// 24-bit Z
	case DXGI_FORMAT_R24G8_TYPELESS:
	case DXGI_FORMAT_D24_UNORM_S8_UINT:
	case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
	case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
		return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;

		// 16-bit Z w/o Stencil
	case DXGI_FORMAT_R16_TYPELESS:
	case DXGI_FORMAT_D16_UNORM:
	case DXGI_FORMAT_R16_UNORM:
		return DXGI_FORMAT_R16_UNORM;
	default:
		return format;
	}
}
