#include "stdafx.h"
#include "ResourceAllocator.h"
#include "Graphics/Core/Texture.h"

Texture* RGResourceAllocator::CreateTexture(const TextureDesc& desc)
{
	for (size_t i = 0; i < m_TextureCache.size(); ++i)
	{
		const TextureDesc& otherDesc = m_TextureCache[i]->GetDesc();
		if (desc.Width == otherDesc.Width
			&& desc.Height == otherDesc.Height
			&& desc.DepthOrArraySize == otherDesc.DepthOrArraySize
			&& desc.Format == otherDesc.Format
			&& desc.Mips == otherDesc.Mips
			&& desc.SampleCount == otherDesc.SampleCount
			&& desc.Usage == otherDesc.Usage
			&& desc.ClearBindingValue.BindingValue == otherDesc.ClearBindingValue.BindingValue
			)
		{
			std::swap(m_TextureCache[i], m_TextureCache.back());
			Texture* pTex = m_TextureCache.back();
			m_TextureCache.pop_back();
			return pTex;
		}
	}
	std::unique_ptr<Texture> pTex = std::make_unique<Texture>(m_pGraphics);
	pTex->Create(desc);
	m_Textures.push_back(std::move(pTex));
	return m_Textures.back().get();
}

void RGResourceAllocator::ReleaseTexture(Texture* pTexture)
{
	m_TextureCache.push_back(pTexture);
}
