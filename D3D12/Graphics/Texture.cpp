#include "stdafx.h"
#include "Texture.h"
#include "Content/Image.h"
#include "CommandContext.h"
#include "Graphics.h"
#include "OfflineDescriptorAllocator.h"
#include "ResourceViews.h"

Texture::Texture(Graphics* pGraphics, const char* pName)
	: GraphicsResource(pGraphics), m_pName(pName)
{
}

Texture::~Texture()
{

}

D3D12_CPU_DESCRIPTOR_HANDLE Texture::GetSRV() const
{
	return m_pSrv->GetDescriptor();
}

D3D12_CPU_DESCRIPTOR_HANDLE Texture::GetDSV(bool writeable /*= true*/) const
{
	return writeable ? m_Rtv : m_ReadOnlyDsv;
}

void Texture::CreateUAV(UnorderedAccessView** pView, const TextureUAVDesc& desc)
{
	if (*pView == nullptr)
	{
		m_Descriptors.push_back(std::make_unique<UnorderedAccessView>(m_pGraphics));
		*pView = static_cast<UnorderedAccessView*>(m_Descriptors.back().get());
	}
	(*pView)->Create(this, desc);
}

void Texture::CreateSRV(ShaderResourceView** pView, const TextureSRVDesc& desc)
{
	if (*pView == nullptr)
	{
		m_Descriptors.push_back(std::make_unique<ShaderResourceView>(m_pGraphics));
		*pView = static_cast<ShaderResourceView*>(m_Descriptors.back().get());
	}
	(*pView)->Create(this, desc);
}

D3D12_CPU_DESCRIPTOR_HANDLE Texture::GetRTV() const
{
	return m_Rtv;
}

D3D12_CPU_DESCRIPTOR_HANDLE Texture::GetUAV() const
{
	return m_pUav->GetDescriptor();
}

void Texture::Create(const TextureDesc& textureDesc)
{
	m_Desc = textureDesc;
	TextureFlag depthAndRt = TextureFlag::RenderTarget | TextureFlag::DepthStencil;
	assert(Any(textureDesc.Usage, depthAndRt) == false);

	Release();

	m_RtvDescriptorSize = m_pGraphics->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	m_SrvUavDescriptorSize = m_pGraphics->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	m_DsvDescriptorSize = m_pGraphics->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

	m_CurrentState = D3D12_RESOURCE_STATE_COMMON;

	D3D12_CLEAR_VALUE * pClearValue = nullptr;
	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format = textureDesc.Format;

	D3D12_RESOURCE_DESC desc = {};
	desc.Alignment = 0; //0 will pick the most appropriate alignment
	desc.Format = textureDesc.Format;
	desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	desc.MipLevels = (uint16)textureDesc.Mips;
	desc.SampleDesc.Count = textureDesc.SampleCount;
	desc.SampleDesc.Quality = m_pGraphics->GetMultiSampleQualityLevel(textureDesc.SampleCount);
	desc.Width = textureDesc.Width;
	desc.Height = textureDesc.Height;
	switch (textureDesc.Dimensions)
	{
	case TextureDimension::Texture1D:
	case TextureDimension::Texture1DArray:
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
		desc.DepthOrArraySize = textureDesc.DepthOrArraySize;
		break;
	case TextureDimension::TextureCube:
	case TextureDimension::TextureCubeArray:
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		desc.DepthOrArraySize = 6 * textureDesc.DepthOrArraySize;
		break;
	case TextureDimension::Texture2D:
	case TextureDimension::Texture2DArray:
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		desc.DepthOrArraySize = textureDesc.DepthOrArraySize;
		break;
	case TextureDimension::Texture3D:
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
		desc.DepthOrArraySize = textureDesc.DepthOrArraySize;
		break;
	default:
		assert(false);
		break;
	}
	if (Any(textureDesc.Usage, TextureFlag::UnorderedAccess))
	{
		desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	}
	if (Any(textureDesc.Usage, TextureFlag::RenderTarget))
	{
		if (textureDesc.ClearBindingValue.BindingValue == ClearBinding::ClearBindingValue::Color)
		{
			memcpy(&clearValue.Color, &textureDesc.ClearBindingValue.Color, sizeof(Color));
		}
		else
		{
			Color clearColor = Color(0, 0, 0, 1);
			memcpy(&clearValue.Color, &clearColor, sizeof(Color));
		}
		pClearValue = &clearValue;

		desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	}
	if (Any(textureDesc.Usage, TextureFlag::DepthStencil))
	{
		if (textureDesc.ClearBindingValue.BindingValue == ClearBinding::ClearBindingValue::DepthStencil)
		{
			clearValue.DepthStencil.Depth = textureDesc.ClearBindingValue.DepthStencil.Depth;
			clearValue.DepthStencil.Stencil = textureDesc.ClearBindingValue.DepthStencil.Stencil;
		}
		else
		{
			clearValue.DepthStencil.Depth = 1;
			clearValue.DepthStencil.Stencil = 0;
		}

		desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		pClearValue = &clearValue;
		m_CurrentState = D3D12_RESOURCE_STATE_DEPTH_WRITE;

		if (Any(textureDesc.Usage, TextureFlag::ShaderResource) == false)
		{
			desc.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
		}
	}

	D3D12_RESOURCE_ALLOCATION_INFO info = m_pGraphics->GetDevice()->GetResourceAllocationInfo(0, 1, &desc);

	m_pResource = m_pGraphics->CreateResource(desc, m_CurrentState, D3D12_HEAP_TYPE_DEFAULT, pClearValue);

	if (Any(textureDesc.Usage, TextureFlag::ShaderResource) )
	{
		CreateSRV(&m_pSrv, TextureSRVDesc(0));
	}
	if (Any(textureDesc.Usage, TextureFlag::UnorderedAccess))
	{
		TextureUAVDesc uavDesc(0);
		CreateUAV(&m_pUav, uavDesc);
	}
	if (Any(textureDesc.Usage, TextureFlag::RenderTarget))
	{
		if (m_Rtv.ptr == 0)
		{
			m_Rtv = m_pGraphics->GetDescriptorManager(D3D12_DESCRIPTOR_HEAP_TYPE_RTV)->AllocateDescriptor();
		}

		D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		rtvDesc.Format = textureDesc.Format;
		switch (textureDesc.Dimensions)
		{
		case TextureDimension::Texture1D:
			rtvDesc.Texture1D.MipSlice = 0;
			rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1D;
			break;
		case TextureDimension::Texture1DArray:
			rtvDesc.Texture1DArray.ArraySize = textureDesc.DepthOrArraySize;
			rtvDesc.Texture1DArray.FirstArraySlice = 0;
			rtvDesc.Texture1DArray.MipSlice = 0;
			rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1DARRAY;
			break;
		case TextureDimension::Texture2D:
			if (textureDesc.SampleCount > 1)
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
			if (textureDesc.SampleCount > 1)
			{
				rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;
			}
			else
			{
				rtvDesc.Texture2DArray.MipSlice = 0;
				rtvDesc.Texture2DArray.PlaneSlice = 0;
				rtvDesc.Texture2DArray.ArraySize = textureDesc.DepthOrArraySize;
				rtvDesc.Texture2DArray.FirstArraySlice = 0;
				rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
			}
			break;
		case TextureDimension::Texture3D:
			rtvDesc.Texture3D.FirstWSlice = 0;
			rtvDesc.Texture3D.MipSlice = 0;
			rtvDesc.Texture3D.WSize = textureDesc.DepthOrArraySize;
			rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
			break;
		default:
			break;
		}
		m_pGraphics->GetDevice()->CreateRenderTargetView(m_pResource, &rtvDesc, m_Rtv);
	}
	else if (Any(textureDesc.Usage, TextureFlag::DepthStencil))
	{
		if (m_Rtv.ptr == 0)
		{
			m_Rtv = m_pGraphics->GetDescriptorManager(D3D12_DESCRIPTOR_HEAP_TYPE_DSV)->AllocateDescriptor();
		}
		if (m_ReadOnlyDsv.ptr == 0)
		{
			m_ReadOnlyDsv = m_pGraphics->GetDescriptorManager(D3D12_DESCRIPTOR_HEAP_TYPE_DSV)->AllocateDescriptor();
		}

		D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
		dsvDesc.Format = textureDesc.Format;
		switch (textureDesc.Dimensions)
		{
		case TextureDimension::Texture1D:
			dsvDesc.Texture1D.MipSlice = 0;
			dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1D;
			break;
		case TextureDimension::Texture1DArray:
			dsvDesc.Texture1DArray.ArraySize = textureDesc.DepthOrArraySize;
			dsvDesc.Texture1DArray.FirstArraySlice = 0;
			dsvDesc.Texture1DArray.MipSlice = 0;
			dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1DARRAY;
			break;
		case TextureDimension::Texture2D:
			if (textureDesc.SampleCount > 1)
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
			if (textureDesc.SampleCount > 1)
			{
				dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY;
			}
			else
			{
				dsvDesc.Texture2DArray.ArraySize = textureDesc.DepthOrArraySize;
				dsvDesc.Texture2DArray.FirstArraySlice = 0;
				dsvDesc.Texture2DArray.MipSlice = 0;
				dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
			}
			break;
		default:
			break;

		}
		m_pGraphics->GetDevice()->CreateDepthStencilView(m_pResource, &dsvDesc, m_Rtv);
		dsvDesc.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH;
		m_pGraphics->GetDevice()->CreateDepthStencilView(m_pResource, &dsvDesc, m_ReadOnlyDsv);
	}

	if (m_pName)
	{
		SetName(m_pName);
	}
}

int Texture::GetRowDataSize(DXGI_FORMAT format, unsigned int width)
{
	switch (format)
	{
	case DXGI_FORMAT_R8_UNORM:
	case DXGI_FORMAT_A8_UNORM:
	case DXGI_FORMAT_R8_UINT:
		return (unsigned)width;

	case DXGI_FORMAT_R8G8_UNORM:
	case DXGI_FORMAT_R16_UNORM:
	case DXGI_FORMAT_R16_FLOAT:
	case DXGI_FORMAT_R16_TYPELESS:
	case DXGI_FORMAT_R16_UINT:
		return (unsigned)(width * 2);

	case DXGI_FORMAT_B8G8R8A8_UNORM:
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
	case DXGI_FORMAT_R16G16_UNORM:
	case DXGI_FORMAT_R16G16_FLOAT:
	case DXGI_FORMAT_R32_FLOAT:
	case DXGI_FORMAT_R24G8_TYPELESS:
	case DXGI_FORMAT_R32_TYPELESS:
	case DXGI_FORMAT_R32_UINT:
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
		assert(false);
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

bool Texture::Create(CommandContext* pContext, const char* pFilePath, bool srgb)
{
	Image img;
	if (img.Load(pFilePath))
	{
		return Create(pContext, img, srgb);
	}
	return false;
}

bool Texture::Create(CommandContext* pContext, const Image& img, bool srgb /*= false*/)
{
	TextureDesc desc;
	desc.Width = img.GetWidth();
	desc.Height = img.GetHeight();
	desc.Format = (DXGI_FORMAT)Image::TextureFormatFromCompressionFormat(img.GetFormat(), srgb);
	desc.Mips = img.GetMipLevels();
	desc.Usage = TextureFlag::ShaderResource;
	desc.Dimensions = img.IsCubemap() ? TextureDimension::TextureCube : TextureDimension::Texture2D;

	const Image* pImg = &img;
	std::vector<D3D12_SUBRESOURCE_DATA> subResourceData;
	int resourceOffset = 0;
	while (pImg)
	{
		subResourceData.resize(subResourceData.size() + desc.Mips);
		for (int i = 0; i < desc.Mips; ++i)
		{
			D3D12_SUBRESOURCE_DATA& data = subResourceData[resourceOffset++];
			MipLevelInfo info = pImg->GetMipInfo(i);
			data.pData = pImg->GetData(i);
			data.RowPitch = info.RowSize;
			data.SlicePitch = (uint64)info.RowSize * info.Width;
		}
		pImg = pImg->GetNextImage();
	}
	Create(desc);
	pContext->InitializeTexture(this, subResourceData.data(), 0, (int)subResourceData.size());
	return true;
}

void Texture::SetData(CommandContext* pContext, const void* pData)
{
	D3D12_SUBRESOURCE_DATA data;
	data.pData = pData;
	data.RowPitch = Texture::GetRowDataSize(m_Desc.Format, m_Desc.Width);
	data.SlicePitch = data.RowPitch * m_Desc.Width;
	pContext->InitializeTexture(this, &data, 0, 1);
}

void Texture::CreateForSwapchain(ID3D12Resource* pTexture)
{
	Release();
	m_pResource = pTexture;
	m_CurrentState = D3D12_RESOURCE_STATE_PRESENT;
	D3D12_RESOURCE_DESC desc = pTexture->GetDesc();

	m_Desc.Width = (uint32)desc.Width;
	m_Desc.Height = (uint32)desc.Height;
	m_Desc.Format = desc.Format;
	m_Desc.ClearBindingValue = ClearBinding(Color(0, 0, 0, 1));
	m_Desc.Mips = desc.MipLevels;
	m_Desc.SampleCount = desc.SampleDesc.Count;
	m_Desc.Usage = TextureFlag::RenderTarget;

	if (m_Rtv.ptr == 0)
	{
		m_Rtv = m_pGraphics->GetDescriptorManager(D3D12_DESCRIPTOR_HEAP_TYPE_RTV)->AllocateDescriptor();
	}
	m_pGraphics->GetDevice()->CreateRenderTargetView(pTexture, nullptr, m_Rtv);
	CreateSRV(&m_pSrv, TextureSRVDesc(0));
}