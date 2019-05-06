#include "stdafx.h"
#include "Texture.h"
#include "Content/Image.h"
#include "CommandContext.h"
#include "Graphics.h"

Texture::Texture(ID3D12Device* pDevice)
	: m_Width(0), m_Height(0), m_DepthOrArraySize(0), m_Format(DXGI_FORMAT_UNKNOWN), m_MipLevels(1)
{
	m_SrvUavDescriptorSize = pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	m_RtvDescriptorSize = pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
}

D3D12_CPU_DESCRIPTOR_HANDLE Texture::GetSRV(int subResource /*= 0*/) const
{
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(m_Srv, subResource, m_SrvUavDescriptorSize);
}

D3D12_CPU_DESCRIPTOR_HANDLE Texture::GetDSV(int subResource /*= 0*/) const
{
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(m_Rtv, subResource, m_RtvDescriptorSize);
}

D3D12_CPU_DESCRIPTOR_HANDLE Texture::GetRTV(int subResource /*= 0*/) const
{
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(m_Rtv, subResource, m_RtvDescriptorSize);
}

D3D12_CPU_DESCRIPTOR_HANDLE Texture::GetUAV(int subResource /*= 0*/) const
{
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(m_Uav, subResource, m_SrvUavDescriptorSize);
}

void Texture::Create_Internal(Graphics* pGraphics, TextureDimension dimension, int width, int height, int depthOrArraySize, DXGI_FORMAT format, TextureUsage usage, int sampleCount)
{
	TextureUsage depthAndRt = TextureUsage::RenderTarget | TextureUsage::DepthStencil;
	assert((usage & depthAndRt) != depthAndRt);

	Release();

	m_Width = width;
	m_Height = height;
	m_DepthOrArraySize = depthOrArraySize;
	m_IsArray = dimension == TextureDimension::Texture1DArray || dimension == TextureDimension::Texture2DArray || dimension == TextureDimension::TextureCubeArray;
	m_Format = format;
	m_SampleCount = sampleCount;
	m_CurrentState = D3D12_RESOURCE_STATE_COMMON;

	D3D12_CLEAR_VALUE * pClearValue = nullptr;
	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format = format;

	D3D12_RESOURCE_DESC desc = {};
	desc.Alignment = 0;
	desc.DepthOrArraySize = 1;
	switch (dimension)
	{
	case TextureDimension::Texture1D:
	case TextureDimension::Texture1DArray:
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
		break;
	case TextureDimension::TextureCube:
	case TextureDimension::TextureCubeArray:
	case TextureDimension::Texture2D:
	case TextureDimension::Texture2DArray:
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		break;
	case TextureDimension::Texture3D:
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
		break;
	default:
		assert(false);
		break;
	}
	if ((usage & TextureUsage::UnorderedAccess) == TextureUsage::UnorderedAccess)
	{
		desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	}
	if ((usage & TextureUsage::RenderTarget) == TextureUsage::RenderTarget)
	{
		desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		Color clearColor = Color(0, 0, 0, 1);
		memcpy(&clearValue.Color, &clearColor, sizeof(Color));
		pClearValue = &clearValue;
	}
	if ((usage & TextureUsage::DepthStencil) == TextureUsage::DepthStencil)
	{
		desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		clearValue.DepthStencil.Depth = 0;
		clearValue.DepthStencil.Stencil = 0;
		pClearValue = &clearValue;
	}
	desc.Format = format;
	desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	desc.MipLevels = (uint16)m_MipLevels;
	desc.SampleDesc.Count = sampleCount;
	desc.SampleDesc.Quality = pGraphics->GetMultiSampleQualityLevel(sampleCount);
	desc.Width = width;
	desc.Height = height;
	desc.DepthOrArraySize = m_DepthOrArraySize;

	D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	HR(pGraphics->GetDevice()->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		m_CurrentState,
		pClearValue,
		IID_PPV_ARGS(&m_pResource)));

	if ((usage & TextureUsage::ShaderResource) == TextureUsage::ShaderResource)
	{
		if (m_Srv.ptr == 0)
		{
			m_Srv = pGraphics->AllocateCpuDescriptors(1, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = (usage & TextureUsage::DepthStencil) == TextureUsage::DepthStencil ? GetSrvFormatFromDepth(format) : format;
		
		switch (dimension)
		{
		case TextureDimension::Texture1D:
			srvDesc.Texture1D.MipLevels = m_MipLevels;
			srvDesc.Texture1D.MostDetailedMip = 0;
			srvDesc.Texture1D.ResourceMinLODClamp = 0;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
			break;
		case TextureDimension::Texture1DArray:
			srvDesc.Texture1DArray.ArraySize = depthOrArraySize;
			srvDesc.Texture1DArray.FirstArraySlice = 0;
			srvDesc.Texture1DArray.MipLevels = m_MipLevels;
			srvDesc.Texture1DArray.MostDetailedMip = 0;
			srvDesc.Texture1DArray.ResourceMinLODClamp = 0;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
			break;
		case TextureDimension::Texture2D:
			if (sampleCount > 1)
			{
				srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
			}
			else
			{
				srvDesc.Texture2D.MipLevels = m_MipLevels;
				srvDesc.Texture2D.MostDetailedMip = 0;
				srvDesc.Texture2D.PlaneSlice = 0;
				srvDesc.Texture2D.ResourceMinLODClamp = 0;
				srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			}
			break;
		case TextureDimension::Texture2DArray:
			if (sampleCount > 1)
			{
				srvDesc.Texture2DMSArray.ArraySize = depthOrArraySize;
				srvDesc.Texture2DMSArray.FirstArraySlice = 0;
				srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
			}
			else
			{
				srvDesc.Texture2DArray.MipLevels = m_MipLevels;
				srvDesc.Texture2DArray.MostDetailedMip = 0;
				srvDesc.Texture2DArray.PlaneSlice = 0;
				srvDesc.Texture2DArray.ResourceMinLODClamp = 0;
				srvDesc.Texture2DArray.ArraySize = depthOrArraySize;
				srvDesc.Texture2DArray.FirstArraySlice = 0;
				srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
			}
			break;
		case TextureDimension::Texture3D:
			srvDesc.Texture3D.MipLevels = m_MipLevels;
			srvDesc.Texture3D.MostDetailedMip = 0;
			srvDesc.Texture3D.ResourceMinLODClamp = 0;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
			break;
		case TextureDimension::TextureCube:
			srvDesc.TextureCube.MipLevels = m_MipLevels;
			srvDesc.TextureCube.MostDetailedMip = 0;
			srvDesc.TextureCube.ResourceMinLODClamp = 0;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
			break;
		case TextureDimension::TextureCubeArray:
			srvDesc.TextureCubeArray.MipLevels = m_MipLevels;
			srvDesc.TextureCubeArray.MostDetailedMip = 0;
			srvDesc.TextureCubeArray.ResourceMinLODClamp = 0;
			srvDesc.TextureCubeArray.First2DArrayFace = 0;
			srvDesc.TextureCubeArray.NumCubes = depthOrArraySize;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
			break;
		default:
			break;
		}
		pGraphics->GetDevice()->CreateShaderResourceView(m_pResource.Get(), &srvDesc, m_Srv);
	}
	if ((usage & TextureUsage::UnorderedAccess) == TextureUsage::UnorderedAccess)
	{
		if (m_Uav.ptr == 0)
		{
			m_Uav = pGraphics->AllocateCpuDescriptors(m_MipLevels, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		switch (dimension)
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
			uavDesc.Texture2DArray.ArraySize = depthOrArraySize;
			uavDesc.Texture2DArray.FirstArraySlice = 0;
			uavDesc.Texture2DArray.PlaneSlice = 0;
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
			break;
		case TextureDimension::Texture3D:
			uavDesc.Texture3D.FirstWSlice = 0;
			uavDesc.Texture3D.WSize = depthOrArraySize;
			break;
		case TextureDimension::TextureCube:
		case TextureDimension::TextureCubeArray:
			//unsupported
			assert(false);
			break;
		default:
			break;
		
		}
		for (int i = 0; i < m_MipLevels; ++i)
		{
			uavDesc.Texture1D.MipSlice = i;
			uavDesc.Texture1DArray.MipSlice = i;
			uavDesc.Texture2D.MipSlice = i;
			uavDesc.Texture2DArray.MipSlice = i;
			uavDesc.Texture3D.MipSlice = i;
			pGraphics->GetDevice()->CreateUnorderedAccessView(m_pResource.Get(), nullptr, &uavDesc, m_Uav.Offset(i, m_SrvUavDescriptorSize));
		}
	}
	if ((usage & TextureUsage::RenderTarget) == TextureUsage::RenderTarget)
	{
		if (m_Rtv.ptr == 0)
		{
			m_Rtv = pGraphics->AllocateCpuDescriptors(1, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		}

		D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		rtvDesc.Format = format;
		switch (dimension)
		{
		case TextureDimension::Texture1D:
			rtvDesc.Texture1D.MipSlice = 0;
			rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1D;
			break;
		case TextureDimension::Texture1DArray:
			rtvDesc.Texture1DArray.ArraySize = depthOrArraySize;
			rtvDesc.Texture1DArray.FirstArraySlice = 0;
			rtvDesc.Texture1DArray.MipSlice = 0;
			rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1DARRAY;
			break;
		case TextureDimension::Texture2D:
			if (sampleCount > 1)
			{
				rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
			}
			else
			{
				rtvDesc.Texture2D.MipSlice = 0;
				rtvDesc.Texture2D.PlaneSlice = 0;
				rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
			}
			break;
		case TextureDimension::TextureCube:
		case TextureDimension::TextureCubeArray:
		case TextureDimension::Texture2DArray:
			if (sampleCount > 1)
			{
				rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;
			}
			else
			{
				rtvDesc.Texture2DArray.MipSlice = 0;
				rtvDesc.Texture2DArray.PlaneSlice = 0;
				rtvDesc.Texture2DArray.ArraySize = depthOrArraySize;
				rtvDesc.Texture2DArray.FirstArraySlice = 0;
				rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
			}
			break;
		case TextureDimension::Texture3D:
			rtvDesc.Texture3D.FirstWSlice = 0;
			rtvDesc.Texture3D.MipSlice = 0;
			rtvDesc.Texture3D.WSize = depthOrArraySize;
			rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
			break;
		default:
			break;
		}
		pGraphics->GetDevice()->CreateRenderTargetView(m_pResource.Get(), &rtvDesc, m_Rtv);
	}
	else if ((usage & TextureUsage::DepthStencil) == TextureUsage::DepthStencil)
	{
		if (m_Rtv.ptr == 0)
		{
			m_Rtv = pGraphics->AllocateCpuDescriptors(1, D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
		}

		D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
		dsvDesc.Format = format;
		switch (dimension)
		{
		case TextureDimension::Texture1D:
			dsvDesc.Texture1D.MipSlice = 0;
			dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1D;
			break;
		case TextureDimension::Texture1DArray:
			dsvDesc.Texture1DArray.ArraySize = depthOrArraySize;
			dsvDesc.Texture1DArray.FirstArraySlice = 0;
			dsvDesc.Texture1DArray.MipSlice = 0;
			dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1DARRAY;
			break;
		case TextureDimension::Texture2D:
			if (sampleCount > 1)
			{
				dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
			}
			else
			{
				dsvDesc.Texture2D.MipSlice = 0;
				dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
			}
			break;
		case TextureDimension::Texture3D:
		case TextureDimension::TextureCube:
		case TextureDimension::TextureCubeArray:
		case TextureDimension::Texture2DArray:
			if (sampleCount > 1)
			{
				dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY;
			}
			else
			{
				dsvDesc.Texture2DArray.ArraySize = depthOrArraySize;
				dsvDesc.Texture2DArray.FirstArraySlice = 0;
				dsvDesc.Texture2DArray.MipSlice = 0;
				dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
			}
			break;
		default:
			break;
			
		}
		pGraphics->GetDevice()->CreateDepthStencilView(m_pResource.Get(), &dsvDesc, m_Rtv);
	}
}

int Texture::GetRowDataSize(DXGI_FORMAT format, unsigned int width)
{
	switch (format)
	{
	case DXGI_FORMAT_R8_UNORM:
	case DXGI_FORMAT_A8_UNORM:
		return (unsigned)width;

	case DXGI_FORMAT_R8G8_UNORM:
	case DXGI_FORMAT_R16_UNORM:
	case DXGI_FORMAT_R16_FLOAT:
	case DXGI_FORMAT_R16_TYPELESS:
		return (unsigned)(width * 2);

	case DXGI_FORMAT_B8G8R8A8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_R16G16_UNORM:
	case DXGI_FORMAT_R16G16_FLOAT:
	case DXGI_FORMAT_R32_FLOAT:
	case DXGI_FORMAT_R24G8_TYPELESS:
	case DXGI_FORMAT_R32_TYPELESS:
		return (unsigned)(width * 4);

	case DXGI_FORMAT_R16G16B16A16_UNORM:
	case DXGI_FORMAT_R16G16B16A16_FLOAT:
		return (unsigned)(width * 8);

	case DXGI_FORMAT_R32G32B32A32_FLOAT:
		return (unsigned)(width * 16);

	case DXGI_FORMAT_BC1_TYPELESS:
	case DXGI_FORMAT_BC1_UNORM:
	case DXGI_FORMAT_BC1_UNORM_SRGB:
	case DXGI_FORMAT_BC4_TYPELESS:
	case DXGI_FORMAT_BC4_UNORM:
	case DXGI_FORMAT_BC4_SNORM:
		return (unsigned)(((width + 3) >> 2) * 8);

	case DXGI_FORMAT_BC2_TYPELESS:
	case DXGI_FORMAT_BC2_UNORM:
	case DXGI_FORMAT_BC2_UNORM_SRGB:
	case DXGI_FORMAT_BC3_TYPELESS:
	case DXGI_FORMAT_BC3_UNORM:
	case DXGI_FORMAT_BC3_UNORM_SRGB:
	case DXGI_FORMAT_BC5_TYPELESS:
	case DXGI_FORMAT_BC5_UNORM:
	case DXGI_FORMAT_BC5_SNORM:
	case DXGI_FORMAT_BC6H_TYPELESS:
	case DXGI_FORMAT_BC6H_UF16:
	case DXGI_FORMAT_BC6H_SF16:
	case DXGI_FORMAT_BC7_TYPELESS:
	case DXGI_FORMAT_BC7_UNORM:
	case DXGI_FORMAT_BC7_UNORM_SRGB:
		return (unsigned)(((width + 3) >> 2) * 16);
	case DXGI_FORMAT_R32G32B32_FLOAT:
		return width * 3 * sizeof(float);
	default:
		return 0;
	}
}

DXGI_FORMAT Texture::GetSrvFormatFromDepth(DXGI_FORMAT format)
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
		return DXGI_FORMAT_UNKNOWN;
	}
}

Texture2D::Texture2D(ID3D12Device* pDevice)
	: Texture(pDevice)
{

}

void Texture2D::Create(Graphics* pGraphics, CommandContext* pContext, const char* pFilePath, TextureUsage usage)
{
	Image img;
	if (img.Load(pFilePath))
	{
		m_Width = img.GetWidth();
		m_Height = img.GetHeight();
		m_Format = (DXGI_FORMAT)Image::TextureFormatFromCompressionFormat(img.GetFormat(), false);
		m_MipLevels = img.GetMipLevels();

		std::vector<D3D12_SUBRESOURCE_DATA> subResourceData(m_MipLevels);
		for (int i = 0; i < m_MipLevels; ++i)
		{
			D3D12_SUBRESOURCE_DATA& data = subResourceData[i];
			MipLevelInfo info = img.GetMipInfo(i);
			data.pData = img.GetData(i);
			data.RowPitch = info.RowSize;
			data.SlicePitch = (uint64)info.RowSize * info.Width;
		}

		Create(pGraphics, m_Width, m_Height, m_Format, usage, 1);
		pContext->InitializeTexture(this, subResourceData.data(), m_MipLevels);
		pContext->ExecuteAndReset(true);
	}
}

void Texture2D::Create(Graphics* pGraphics, int width, int height, DXGI_FORMAT format, TextureUsage usage, int sampleCount, int arraySize /*= -1*/)
{
	if (arraySize != -1)
	{
		Create_Internal(pGraphics, TextureDimension::Texture2DArray, width, height, arraySize, format, usage, sampleCount);
	}
	else
	{
		Create_Internal(pGraphics, TextureDimension::Texture2D, width, height, 1, format, usage, sampleCount);
	}
}

void Texture2D::SetData(CommandContext* pContext, const void* pData)
{
	D3D12_SUBRESOURCE_DATA data;
	data.pData = pData;
	data.RowPitch = Texture::GetRowDataSize(m_Format, m_Width);
	data.SlicePitch = data.RowPitch * m_Width;
	pContext->InitializeTexture(this, &data, 1);
}

void Texture2D::CreateForSwapchain(Graphics* pGraphics, ID3D12Resource* pTexture)
{
	m_pResource = pTexture;
	m_CurrentState = D3D12_RESOURCE_STATE_PRESENT;
	D3D12_RESOURCE_DESC desc = pTexture->GetDesc();
	m_Width = (uint32)desc.Width;
	m_Height = (uint32)desc.Height;
	m_Format = desc.Format;
	if (m_Rtv.ptr == 0)
	{
		m_Rtv = pGraphics->AllocateCpuDescriptors(1, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	}
	pGraphics->GetDevice()->CreateRenderTargetView(pTexture, nullptr, m_Rtv);
}

Texture3D::Texture3D(ID3D12Device* pDevice)
	: Texture(pDevice)
{
}

void Texture3D::Create(Graphics* pGraphics, int width, int height, int depth, DXGI_FORMAT format, TextureUsage usage)
{
	Create_Internal(pGraphics, TextureDimension::Texture3D, width, height, depth, format, usage, 1);
}

TextureCube::TextureCube(ID3D12Device* pDevice)
	: Texture(pDevice)
{

}

void TextureCube::Create(Graphics* pGraphics, int width, int height, DXGI_FORMAT format, TextureUsage usage, int sampleCount, int arraySize /*= -1*/)
{
	if (arraySize != -1)
	{
		Create_Internal(pGraphics, TextureDimension::TextureCubeArray, width, height, arraySize, format, usage, sampleCount);
	}
	else
	{
		Create_Internal(pGraphics, TextureDimension::TextureCube, width, height, 1, format, usage, sampleCount);
	}
}
