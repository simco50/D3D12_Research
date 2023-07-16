#pragma once
#include "RenderGraphDefinitions.h"
#include "Graphics/RHI/GraphicsResource.h"

class RGResourcePool : public GraphicsObject
{
public:
	RGResourcePool(GraphicsDevice* pDevice)
		: GraphicsObject(pDevice)
	{}

	NO_DISCARD RefCountPtr<Texture> Allocate(const char* pName, const TextureDesc& desc);
	NO_DISCARD RefCountPtr<Buffer> Allocate(const char* pName, const BufferDesc& desc);
	void Tick();

private:
	template<typename T>
	struct PooledResource
	{
		RefCountPtr<T> pResource;
		uint32 LastUsedFrame;
	};
	using PooledTexture = PooledResource<Texture>;
	using PooledBuffer = PooledResource<Buffer>;
	std::vector<PooledTexture> m_TexturePool;
	std::vector<PooledBuffer> m_BufferPool;
	uint32 m_FrameIndex = 0;
};
