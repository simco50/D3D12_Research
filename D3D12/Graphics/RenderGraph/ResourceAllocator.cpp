#include "stdafx.h"
#include "ResourceAllocator.h"
#include "../Texture.h"

namespace RG
{
	Texture* ResourceAllocator::CreateTexture(const TextureDesc& desc)
	{
		for (size_t i = 0; i < m_TextureCache.size(); ++i)
		{
			const TextureDesc& otherDesc = m_TextureCache[i]->GetDesc();
			if (desc.Width == otherDesc.Width
				&& desc.Height == otherDesc.Height
				&& desc.DepthOrArraySize == otherDesc.DepthOrArraySize
				&& desc.Format == desc.Format
				&& desc.Mips == desc.Mips
				&& desc.SampleCount == desc.SampleCount
				&& desc.Usage == desc.Usage
				&& desc.ClearBindingValue.BindingValue == desc.ClearBindingValue.BindingValue
				)
			{
				std::swap(m_TextureCache[i], m_TextureCache.back());
				Texture* pTex = m_TextureCache.back();
				m_TextureCache.pop_back();
				return pTex;
			}
		}
		std::unique_ptr<Texture> pTex = std::make_unique<Texture>();
		pTex->Create(m_pGraphics, desc);
		m_Textures.push_back(std::move(pTex));
		return m_Textures.back().get();
	}

	void ResourceAllocator::ReleaseTexture(Texture* pTexture)
	{
		m_TextureCache.push_back(pTexture);
	}
}