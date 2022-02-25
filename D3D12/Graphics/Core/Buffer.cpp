#include "stdafx.h"
#include "Buffer.h"
#include "CommandContext.h"
#include "Graphics.h"
#include "ResourceViews.h"

Buffer::Buffer(GraphicsDevice* pParent, const char* pName /*= ""*/)
	: GraphicsResource(pParent)
{
	m_Name = pName;
}

Buffer::Buffer(GraphicsDevice* pParent, ID3D12Resource* pResource, D3D12_RESOURCE_STATES state)
	: GraphicsResource(pParent, pResource, state)
{

}

void Buffer::CreateUAV(UnorderedAccessView** pView, const BufferUAVDesc& desc)
{
	if (*pView == nullptr)
	{
		m_Descriptors.push_back(std::make_unique<UnorderedAccessView>());
		*pView = static_cast<UnorderedAccessView*>(m_Descriptors.back().get());
	}
	(*pView)->Create(this, desc);
}

void Buffer::CreateSRV(ShaderResourceView** pView, const BufferSRVDesc& desc)
{
	if (*pView == nullptr)
	{
		m_Descriptors.push_back(std::make_unique<ShaderResourceView>());
		*pView = static_cast<ShaderResourceView*>(m_Descriptors.back().get());
	}
	(*pView)->Create(this, desc);
}

uint32 Buffer::GetSRVIndex() const
{
	return m_pSrv ? m_pSrv->GetHeapIndex() : DescriptorHandle::InvalidHeapIndex;
}

uint32 Buffer::GetUAVIndex() const
{
	return m_pUav ? m_pUav->GetHeapIndex() : DescriptorHandle::InvalidHeapIndex;
}
