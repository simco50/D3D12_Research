#include "stdafx.h"
#include "RenderGraphResourcePool.h"
#include "Graphics/RHI/Graphics.h"

RefCountPtr<Texture> RGResourcePool::Allocate(const char* pName, const TextureDesc& desc)
{
	for (PooledTexture& texture : m_TexturePool)
	{
		RefCountPtr<Texture>& pTexture = texture.pResource;
		if (pTexture->GetNumRefs() == 1 && pTexture->GetDesc().IsCompatible(desc))
		{
			texture.LastUsedFrame = m_FrameIndex;
			pTexture->SetName(pName);
			return pTexture;
		}
	}
	return m_TexturePool.emplace_back(PooledTexture{ GetParent()->CreateTexture(desc, pName), m_FrameIndex }).pResource;
}

RefCountPtr<Buffer> RGResourcePool::Allocate(const char* pName, const BufferDesc& desc)
{
	for (PooledBuffer& buffer : m_BufferPool)
	{
		RefCountPtr<Buffer>& pBuffer = buffer.pResource;
		if (pBuffer->GetNumRefs() == 1 && pBuffer->GetDesc().IsCompatible(desc))
		{
			buffer.LastUsedFrame = m_FrameIndex;
			pBuffer->SetName(pName);
			return pBuffer;
		}
	}
	return m_BufferPool.emplace_back(PooledBuffer{ GetParent()->CreateBuffer(desc, pName), m_FrameIndex }).pResource;
}

void RGResourcePool::Tick()
{
	constexpr uint32 numFrameRetention = 5;

	for (uint32 i = 0; i < (uint32)m_TexturePool.size();)
	{
		PooledTexture& texture = m_TexturePool[i];
		if (texture.pResource->GetNumRefs() == 1 && texture.LastUsedFrame + numFrameRetention < m_FrameIndex)
		{
			std::swap(m_TexturePool[i], m_TexturePool.back());
			m_TexturePool.pop_back();
		}
		else
		{
			++i;
		}
	}
	for (uint32 i = 0; i < (uint32)m_BufferPool.size();)
	{
		PooledBuffer& buffer = m_BufferPool[i];
		if (buffer.pResource->GetNumRefs() == 1 && buffer.LastUsedFrame + numFrameRetention < m_FrameIndex)
		{
			std::swap(m_BufferPool[i], m_BufferPool.back());
			m_BufferPool.pop_back();
		}
		else
		{
			++i;
		}
	}
	++m_FrameIndex;
}
