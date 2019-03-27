#include "stdafx.h"
#include "GraphicsResource.h"
#include "CommandContext.h"
#include "Graphics.h"
#include "Image.h"

void GraphicsBuffer::Create(ID3D12Device* pDevice, uint32 size, bool cpuVisible, bool unorderedAccess)
{
	m_Size = size;
	D3D12_RESOURCE_DESC desc = {};
	desc.Alignment = 0;
	desc.DepthOrArraySize = 1;
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Flags = unorderedAccess ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.Height = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Width = size;

	D3D12_HEAP_PROPERTIES properties = CD3DX12_HEAP_PROPERTIES(cpuVisible ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT);
	HR(pDevice->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&m_pResource)));
	m_CurrentState = D3D12_RESOURCE_STATE_COMMON;
}

void GraphicsBuffer::SetData(CommandContext* pContext, void* pData, uint32 dataSize, uint32 offset)
{
	assert(dataSize + offset <= m_Size);
	pContext->InitializeBuffer(this, pData, dataSize);
}

void GraphicsResource::SetName(const std::string& name)
{
#ifdef _DEBUG
	if (m_pResource)
	{
		std::wstring n(name.begin(), name.end());
		m_pResource->SetName(n.c_str());
	}
#endif
}

void StructuredBuffer::Create(Graphics* pGraphics, uint32 elementStride, uint32 elementCount, bool cpuVisible /*= false*/)
{
	if (m_pResource)
	{
		m_pResource->Release();
	}

	m_Size = elementCount * elementStride;
	const int alignment = 16;
	int bufferSize = (m_Size + (alignment - 1)) & ~(alignment - 1);

	m_Size = elementCount * elementStride;
	D3D12_RESOURCE_DESC desc = {};
	desc.Alignment = 0;
	desc.DepthOrArraySize = 1;
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.Height = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Width = bufferSize;

	D3D12_HEAP_PROPERTIES properties = CD3DX12_HEAP_PROPERTIES(cpuVisible ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT);
	HR(pGraphics->GetDevice()->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_pResource)));
	m_CurrentState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
	uavDesc.Buffer.CounterOffsetInBytes = 0;
	uavDesc.Buffer.FirstElement = 0;
	uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
	uavDesc.Buffer.NumElements = elementCount;
	uavDesc.Buffer.StructureByteStride = elementStride;
	uavDesc.Format = DXGI_FORMAT_UNKNOWN;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;

	if (m_Uav.ptr == 0)
	{
		m_Uav = pGraphics->AllocateCpuDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}
	pGraphics->GetDevice()->CreateUnorderedAccessView(m_pResource, nullptr, &uavDesc, m_Uav);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
	srvDesc.Buffer.NumElements = elementCount;
	srvDesc.Buffer.StructureByteStride = elementStride;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;

	if (m_Srv.ptr == 0)
	{
		m_Srv = pGraphics->AllocateCpuDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}
	pGraphics->GetDevice()->CreateShaderResourceView(m_pResource, &srvDesc, m_Srv);
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
			data.SlicePitch = info.RowSize * info.Width;
		}

		Create(pGraphics, m_Width, m_Height, m_Format, usage, 1);
		pContext->InitializeTexture(this, subResourceData.data(), m_MipLevels);
		pContext->ExecuteAndReset(true);
	}
}

void Texture2D::Create(Graphics* pGraphics, int width, int height, DXGI_FORMAT format, TextureUsage usage, int sampleCount)
{
	if (m_pResource)
	{
		m_pResource->Release();
	}

	m_Format = format;
	m_SampleCount = sampleCount;
	TextureUsage depthAndRt = TextureUsage::RenderTarget | TextureUsage::DepthStencil;
	assert((usage & depthAndRt) != depthAndRt);

	m_Width = width;
	m_Height = height;

	D3D12_CLEAR_VALUE* pClearValue = nullptr;
	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format = format;

	D3D12_RESOURCE_STATES initState = D3D12_RESOURCE_STATE_COMMON;

	D3D12_RESOURCE_DESC desc = {};
	desc.Alignment = 0;
	desc.DepthOrArraySize = 1;
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
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

	D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

	HR(pGraphics->GetDevice()->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		initState,
		pClearValue,
		IID_PPV_ARGS(&m_pResource)));
	m_CurrentState = initState;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	if ((usage & TextureUsage::DepthStencil) == TextureUsage::DepthStencil)
	{
		srvDesc.Format = GetDepthFormat(format);
	}
	else
	{
		srvDesc.Format = format;
	}
	srvDesc.Texture2D.MipLevels = m_MipLevels;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.PlaneSlice = 0;
	srvDesc.Texture2D.ResourceMinLODClamp = 0;
	if (m_SampleCount > 1)
	{
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
	}
	else
	{
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	}

	if ((usage & TextureUsage::ShaderResource) == TextureUsage::ShaderResource)
	{
		if (m_Srv.ptr == 0)
		{
			m_Srv = pGraphics->AllocateCpuDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}
		pGraphics->GetDevice()->CreateShaderResourceView(m_pResource, &srvDesc, m_Srv);
	}
	if ((usage & TextureUsage::UnorderedAccess) == TextureUsage::UnorderedAccess)
	{
		if (m_Uav.ptr == 0)
		{
			m_Uav = pGraphics->AllocateCpuDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}
		pGraphics->GetDevice()->CreateUnorderedAccessView(m_pResource, nullptr, nullptr, m_Uav);
	}
	if ((usage & TextureUsage::RenderTarget) == TextureUsage::RenderTarget)
	{
		if (m_Rtv.ptr == 0)
		{
			m_Rtv = pGraphics->AllocateCpuDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		}
		pGraphics->GetDevice()->CreateRenderTargetView(m_pResource, nullptr, m_Rtv);
	}
	else if ((usage & TextureUsage::DepthStencil) == TextureUsage::DepthStencil)
	{
		if (m_Rtv.ptr == 0)
		{
			m_Rtv = pGraphics->AllocateCpuDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
		}
		pGraphics->GetDevice()->CreateDepthStencilView(m_pResource, nullptr, m_Rtv);
	}
}

void Texture2D::SetData(CommandContext* pContext, const void* pData)
{
	D3D12_SUBRESOURCE_DATA data;
	data.pData = pData;
	data.RowPitch = GetRowDataSize(m_Width);
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
		m_Rtv = pGraphics->AllocateCpuDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	}
	pGraphics->GetDevice()->CreateRenderTargetView(pTexture, nullptr, m_Rtv);
}

DXGI_FORMAT Texture::GetDepthFormat(DXGI_FORMAT format)
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

int Texture::GetRowDataSize(unsigned int width) const
{
	switch (m_Format)
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

void TextureCube::Create(Graphics* pGraphics, int width, int height, DXGI_FORMAT format, TextureUsage usage, int sampleCount)
{
	if (m_pResource)
	{
		m_pResource->Release();
	}

	m_Format = format;
	m_SampleCount = sampleCount;
	TextureUsage depthAndRt = TextureUsage::RenderTarget | TextureUsage::DepthStencil;
	assert((usage & depthAndRt) != depthAndRt);
	assert((usage & TextureUsage::UnorderedAccess) != TextureUsage::UnorderedAccess);

	m_Width = width;
	m_Height = height;

	D3D12_CLEAR_VALUE* pClearValue = nullptr;
	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format = format;

	D3D12_RESOURCE_STATES initState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

	D3D12_RESOURCE_DESC desc = {};
	desc.Alignment = 0;
	desc.DepthOrArraySize = 6;
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	if ((usage & TextureUsage::RenderTarget) == TextureUsage::RenderTarget)
	{
		desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		Color clearColor = Color(0, 0, 0, 1);
		memcpy(&clearValue.Color, &clearColor, sizeof(Color));
		pClearValue = &clearValue;
		initState = D3D12_RESOURCE_STATE_RENDER_TARGET;
	}
	if ((usage & TextureUsage::DepthStencil) == TextureUsage::DepthStencil)
	{
		desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		clearValue.DepthStencil.Depth = 1;
		clearValue.DepthStencil.Stencil = 0;
		pClearValue = &clearValue;
		initState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	}
	desc.Format = format;
	desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	desc.MipLevels = (uint16)m_MipLevels;
	desc.SampleDesc.Count = sampleCount;
	desc.SampleDesc.Quality = pGraphics->GetMultiSampleQualityLevel(sampleCount);
	desc.Width = width;
	desc.Height = height;

	D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

	HR(pGraphics->GetDevice()->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		initState,
		pClearValue,
		IID_PPV_ARGS(&m_pResource)));
	m_CurrentState = initState;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	if ((usage & TextureUsage::DepthStencil) == TextureUsage::DepthStencil)
	{
		srvDesc.Format = GetDepthFormat(format);
	}
	else
	{
		srvDesc.Format = format;
	}
	srvDesc.TextureCube.MipLevels = m_MipLevels;
	srvDesc.TextureCube.MostDetailedMip = 0;
	srvDesc.TextureCube.ResourceMinLODClamp = 0;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;

	if ((usage & TextureUsage::ShaderResource) == TextureUsage::ShaderResource)
	{
		if (m_Srv.ptr == 0)
		{
			m_Srv = pGraphics->AllocateCpuDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}
		pGraphics->GetDevice()->CreateShaderResourceView(m_pResource, &srvDesc, m_Srv);
	}
	if ((usage & TextureUsage::RenderTarget) == TextureUsage::RenderTarget)
	{
		for (int i = 0; i < 6; ++i)
		{
			if (m_Rtv[i].ptr == 0)
			{
				m_Rtv[i] = pGraphics->AllocateCpuDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
			}
			D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = { };
			rtvDesc.Texture2D.PlaneSlice = i;
			rtvDesc.Texture2D.MipSlice = 0;
			rtvDesc.Format = format;
			rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
			pGraphics->GetDevice()->CreateRenderTargetView(m_pResource, nullptr, m_Rtv[i]);
		}
	}
	else if ((usage & TextureUsage::DepthStencil) == TextureUsage::DepthStencil)
	{
		for (int i = 0; i < 6; ++i)
		{
			if (m_Rtv[i].ptr == 0)
			{
				m_Rtv[i] = pGraphics->AllocateCpuDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
			}
			D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
			dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
			dsvDesc.Format = GetDepthFormat(format);
			dsvDesc.Texture2D.MipSlice = 0;
			dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
			pGraphics->GetDevice()->CreateDepthStencilView(m_pResource, nullptr, m_Rtv[i]);
		}
	}
}