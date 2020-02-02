#include "stdafx.h"
#include "ResourceViews.h"
#include "Graphics.h"
#include "GraphicsBuffer.h"
#include "Texture.h"

void ShaderResourceView::Create(Graphics* pGraphics, Buffer* pBuffer, const BufferSRVDesc& desc)
{
	assert(pBuffer);
	m_pParent = pBuffer;
	const BufferDesc& bufferDesc = pBuffer->GetDesc();

	if (m_Descriptor.ptr == 0)
	{
		m_Descriptor = pGraphics->AllocateCpuDescriptors(1, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = desc.Format;
	srvDesc.Buffer.FirstElement = desc.FirstElement;
	srvDesc.Buffer.NumElements = bufferDesc.ElementCount;
	srvDesc.Buffer.StructureByteStride = 0;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	if (Any(bufferDesc.Usage, BufferFlag::ByteAddress))
	{
		srvDesc.Buffer.Flags |= D3D12_BUFFER_SRV_FLAG_RAW;
	}
	else if (Any(bufferDesc.Usage, BufferFlag::Structured))
	{
		srvDesc.Buffer.StructureByteStride = bufferDesc.ElementSize;
	}
	pGraphics->GetDevice()->CreateShaderResourceView(pBuffer->GetResource(), &srvDesc, m_Descriptor);
}

void ShaderResourceView::Create(Graphics* pGraphics, Texture* pTexture, const TextureSRVDesc& desc)
{
	assert(pTexture);
	m_pParent = pTexture;
	const TextureDesc& textureDesc = pTexture->GetDesc();

	if (m_Descriptor.ptr == 0)
	{
		m_Descriptor = pGraphics->AllocateCpuDescriptors(1, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = Any(textureDesc.Usage, TextureFlag::DepthStencil) ? Texture::GetSrvFormatFromDepth(textureDesc.Format) : textureDesc.Format;

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
		if (textureDesc.SampleCount > 1)
		{
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
		}
		else
		{
			srvDesc.Texture2D.MipLevels = textureDesc.Mips;
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.PlaneSlice = 0;
			srvDesc.Texture2D.ResourceMinLODClamp = 0;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		}
		break;
	case TextureDimension::Texture2DArray:
		if (textureDesc.SampleCount > 1)
		{
			srvDesc.Texture2DMSArray.ArraySize = textureDesc.DepthOrArraySize;
			srvDesc.Texture2DMSArray.FirstArraySlice = 0;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
		}
		else
		{
			srvDesc.Texture2DArray.MipLevels = textureDesc.Mips;
			srvDesc.Texture2DArray.MostDetailedMip = 0;
			srvDesc.Texture2DArray.PlaneSlice = 0;
			srvDesc.Texture2DArray.ResourceMinLODClamp = 0;
			srvDesc.Texture2DArray.ArraySize = textureDesc.DepthOrArraySize;
			srvDesc.Texture2DArray.FirstArraySlice = 0;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
		}
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
	pGraphics->GetDevice()->CreateShaderResourceView(pTexture->GetResource(), &srvDesc, m_Descriptor);
}

void UnorderedAccessView::Create(Graphics* pGraphics, Buffer* pBuffer, const BufferUAVDesc& desc)
{
	assert(pBuffer);
	m_pParent = pBuffer;
	const BufferDesc& bufferDesc = pBuffer->GetDesc();

	if (m_Descriptor.ptr == 0)
	{
		m_Descriptor = pGraphics->AllocateCpuDescriptors(1, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.Format = desc.Format;
	uavDesc.Buffer.CounterOffsetInBytes = desc.CounterOffset;
	uavDesc.Buffer.FirstElement = desc.FirstElement;
	uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
	uavDesc.Buffer.NumElements = bufferDesc.ElementCount;
	uavDesc.Buffer.StructureByteStride = 0;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;

	if (Any(bufferDesc.Usage, BufferFlag::ByteAddress))
	{
		uavDesc.Buffer.Flags |= D3D12_BUFFER_UAV_FLAG_RAW;
	}
	else if (Any(bufferDesc.Usage, BufferFlag::Structured))
	{
		uavDesc.Buffer.StructureByteStride = bufferDesc.ElementSize;
	}
	pGraphics->GetDevice()->CreateUnorderedAccessView(pBuffer->GetResource(), desc.pCounter ? desc.pCounter->GetResource() : nullptr, &uavDesc, m_Descriptor);
}

void UnorderedAccessView::Create(Graphics* pGraphics, Texture* pTexture, const TextureUAVDesc& desc)
{
	assert(pTexture);
	m_pParent = pTexture;
	const TextureDesc& textureDesc = pTexture->GetDesc();

	if (m_Descriptor.ptr == 0)
	{
		m_Descriptor = pGraphics->AllocateCpuDescriptors(1, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}
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
	pGraphics->GetDevice()->CreateUnorderedAccessView(pTexture->GetResource(), nullptr, &uavDesc, m_Descriptor);
}
