#include "stdafx.h"
#include "ResourceViews.h"
#include "Graphics.h"
#include "GraphicsBuffer.h"
#include "Texture.h"
#include "OfflineDescriptorAllocator.h"

ShaderResourceView::ShaderResourceView(Graphics* pGraphics)
	: ResourceView(pGraphics)
{
	m_Descriptor = m_pGraphics->AllocateDescriptor<D3D12_SHADER_RESOURCE_VIEW_DESC>();
}

ShaderResourceView::~ShaderResourceView()
{
	Release();
}

void ShaderResourceView::Create(Buffer* pBuffer, const BufferSRVDesc& desc)
{
	check(pBuffer);
	m_pParent = pBuffer;
	const BufferDesc& bufferDesc = pBuffer->GetDesc();

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	if (EnumHasAnyFlags(bufferDesc.Usage, BufferFlag::AccelerationStructure))
	{
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
		srvDesc.RaytracingAccelerationStructure.Location = pBuffer->GetGpuHandle();
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		m_pParent->GetGraphics()->GetDevice()->CreateShaderResourceView(nullptr, &srvDesc, m_Descriptor);
	}
	else
	{
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = desc.Format;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = bufferDesc.ElementCount;
		srvDesc.Buffer.StructureByteStride = 0;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		if (desc.Raw)
		{
			srvDesc.Buffer.Flags |= D3D12_BUFFER_SRV_FLAG_RAW;
			srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
		}
		else
		{
			srvDesc.Buffer.StructureByteStride = bufferDesc.ElementSize;
		}
		m_pParent->GetGraphics()->GetDevice()->CreateShaderResourceView(pBuffer->GetResource(), &srvDesc, m_Descriptor);
	}
}

void ShaderResourceView::Create(Texture* pTexture, const TextureSRVDesc& desc)
{
	check(pTexture);
	m_pParent = pTexture;
	const TextureDesc& textureDesc = pTexture->GetDesc();

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = Texture::GetSrvFormat(textureDesc.Format);

	switch (textureDesc.Dimensions)
	{
	case TextureDimension::Texture1D:
		srvDesc.Texture1D.MipLevels = textureDesc.Mips;
		srvDesc.Texture1D.MostDetailedMip = 0;
		srvDesc.Texture1D.ResourceMinLODClamp = 0;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
		break;
	case TextureDimension::Texture1DArray:
		srvDesc.Texture1DArray.ArraySize = textureDesc.DepthOrArraySize;
		srvDesc.Texture1DArray.FirstArraySlice = 0;
		srvDesc.Texture1DArray.MipLevels = textureDesc.Mips;
		srvDesc.Texture1DArray.MostDetailedMip = 0;
		srvDesc.Texture1DArray.ResourceMinLODClamp = 0;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
		break;
	case TextureDimension::Texture2D:
		srvDesc.Texture2D.MipLevels = textureDesc.Mips;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.PlaneSlice = 0;
		srvDesc.Texture2D.ResourceMinLODClamp = 0;
		srvDesc.ViewDimension = textureDesc.SampleCount > 1 ? D3D12_SRV_DIMENSION_TEXTURE2DMS : D3D12_SRV_DIMENSION_TEXTURE2D;
		break;
	case TextureDimension::Texture2DArray:
		srvDesc.Texture2DArray.MipLevels = textureDesc.Mips;
		srvDesc.Texture2DArray.MostDetailedMip = 0;
		srvDesc.Texture2DArray.PlaneSlice = 0;
		srvDesc.Texture2DArray.ResourceMinLODClamp = 0;
		srvDesc.Texture2DArray.ArraySize = textureDesc.DepthOrArraySize;
		srvDesc.Texture2DArray.FirstArraySlice = 0;
		srvDesc.ViewDimension = textureDesc.SampleCount > 1 ? D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY : D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
		break;
	case TextureDimension::Texture3D:
		srvDesc.Texture3D.MipLevels = textureDesc.Mips;
		srvDesc.Texture3D.MostDetailedMip = 0;
		srvDesc.Texture3D.ResourceMinLODClamp = 0;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
		break;
	case TextureDimension::TextureCube:
		srvDesc.TextureCube.MipLevels = textureDesc.Mips;
		srvDesc.TextureCube.MostDetailedMip = 0;
		srvDesc.TextureCube.ResourceMinLODClamp = 0;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		break;
	case TextureDimension::TextureCubeArray:
		srvDesc.TextureCubeArray.MipLevels = textureDesc.Mips;
		srvDesc.TextureCubeArray.MostDetailedMip = 0;
		srvDesc.TextureCubeArray.ResourceMinLODClamp = 0;
		srvDesc.TextureCubeArray.First2DArrayFace = 0;
		srvDesc.TextureCubeArray.NumCubes = textureDesc.DepthOrArraySize;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
		break;
	default:
		break;
	}
	m_pParent->GetGraphics()->GetDevice()->CreateShaderResourceView(pTexture->GetResource(), &srvDesc, m_Descriptor);
}

void ShaderResourceView::Release()
{
	if (m_Descriptor.ptr != 0)
	{
		check(m_pParent);
		m_pParent->GetGraphics()->FreeDescriptor<D3D12_SHADER_RESOURCE_VIEW_DESC>(m_Descriptor);
		m_Descriptor.ptr = 0;
	}
}

UnorderedAccessView::UnorderedAccessView(Graphics* pGraphics)
	: ResourceView(pGraphics)
{
	m_Descriptor = m_pGraphics->AllocateDescriptor<D3D12_UNORDERED_ACCESS_VIEW_DESC>();
}

UnorderedAccessView::~UnorderedAccessView()
{
	Release();
}

void UnorderedAccessView::Create(Buffer* pBuffer, const BufferUAVDesc& desc)
{
	check(pBuffer);
	m_pParent = pBuffer;
	const BufferDesc& bufferDesc = pBuffer->GetDesc();

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.Format = desc.Format;
	uavDesc.Buffer.CounterOffsetInBytes = 0;
	uavDesc.Buffer.FirstElement = 0;
	uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
	uavDesc.Buffer.NumElements = bufferDesc.ElementCount;
	uavDesc.Buffer.StructureByteStride = 0;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;

	if (desc.Raw)
	{
		uavDesc.Buffer.Flags |= D3D12_BUFFER_UAV_FLAG_RAW;
		uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	}
	else
	{
		uavDesc.Buffer.StructureByteStride = bufferDesc.ElementSize;
	}

	if (desc.Counter)
	{
		if (!m_pCounter)
		{
			std::stringstream str;
			str << pBuffer->GetName() << " - Counter";
			m_pCounter = std::make_unique<Buffer>(m_pGraphics, str.str().c_str());
		}
		m_pCounter->Create(BufferDesc::CreateByteAddress(4));
	}
	m_pParent->GetGraphics()->GetDevice()->CreateUnorderedAccessView(pBuffer->GetResource(), m_pCounter ? m_pCounter->GetResource() : nullptr, &uavDesc, m_Descriptor);
}

void UnorderedAccessView::Create(Texture* pTexture, const TextureUAVDesc& desc)
{
	check(pTexture);
	m_pParent = pTexture;
	const TextureDesc& textureDesc = pTexture->GetDesc();

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	switch (textureDesc.Dimensions)
	{
	case TextureDimension::Texture1D:
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
		break;
	case TextureDimension::Texture1DArray:
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
		break;
	case TextureDimension::Texture2D:
		uavDesc.Texture2D.PlaneSlice = 0;
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		break;
	case TextureDimension::Texture2DArray:
		uavDesc.Texture2DArray.ArraySize = textureDesc.DepthOrArraySize;
		uavDesc.Texture2DArray.FirstArraySlice = 0;
		uavDesc.Texture2DArray.PlaneSlice = 0;
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
		break;
	case TextureDimension::Texture3D:
		uavDesc.Texture3D.FirstWSlice = 0;
		uavDesc.Texture3D.WSize = textureDesc.DepthOrArraySize;
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
		break;
	case TextureDimension::TextureCube:
	case TextureDimension::TextureCubeArray:
		uavDesc.Texture2DArray.ArraySize = textureDesc.DepthOrArraySize * 6;
		uavDesc.Texture2DArray.FirstArraySlice = 0;
		uavDesc.Texture2DArray.PlaneSlice = 0;
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
		break;
	default:
		break;
	}
	uavDesc.Texture1D.MipSlice = desc.MipLevel;
	uavDesc.Texture1DArray.MipSlice = desc.MipLevel;
	uavDesc.Texture2D.MipSlice = desc.MipLevel;
	uavDesc.Texture2DArray.MipSlice = desc.MipLevel;
	uavDesc.Texture3D.MipSlice = desc.MipLevel;

	m_pParent->GetGraphics()->GetDevice()->CreateUnorderedAccessView(pTexture->GetResource(), nullptr, &uavDesc, m_Descriptor);
}

void UnorderedAccessView::Release()
{
	if (m_Descriptor.ptr != 0)
	{
		check(m_pParent);
		m_pParent->GetGraphics()->FreeDescriptor<D3D12_UNORDERED_ACCESS_VIEW_DESC>(m_Descriptor);
		m_Descriptor.ptr = 0;
	}
}

UnorderedAccessView* UnorderedAccessView::GetCounterUAV() const
{
	return m_pCounter ? m_pCounter->GetUAV() : nullptr;
}

ShaderResourceView* UnorderedAccessView::GetCounterSRV() const
{
	return m_pCounter ? m_pCounter->GetSRV() : nullptr;
}

ResourceView::ResourceView(Graphics* pGraphics)
	: GraphicsObject(pGraphics)
{
}
