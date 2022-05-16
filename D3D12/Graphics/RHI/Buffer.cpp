#include "stdafx.h"
#include "Buffer.h"

Buffer::Buffer(GraphicsDevice* pParent, const BufferDesc& desc, ID3D12Resource* pResource)
	: GraphicsResource(pParent, pResource), m_Desc(desc)
{
}

