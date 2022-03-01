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

D3D12_CPU_DESCRIPTOR_HANDLE Texture::GetRTV() const
{
	check(EnumHasAllFlags(m_Desc.Usage, TextureFlag::RenderTarget));
	return m_Rtv;
}
