#include "stdafx.h"
#include "GraphicsResource.h"
#include "CommandContext.h"
#define STB_IMAGE_IMPLEMENTATION
#include "External/Stb/stb_image.h"
#include "Graphics.h"

void GraphicsBuffer::Create(ID3D12Device* pDevice, uint32 size, bool cpuVisible /*= false*/)
{
	m_Size = size;
	D3D12_RESOURCE_DESC desc = {};
	desc.Alignment = 0;
	desc.DepthOrArraySize = 1;
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Flags = D3D12_RESOURCE_FLAG_NONE;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.Height = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Width = size;

	D3D12_HEAP_PROPERTIES properties = CD3DX12_HEAP_PROPERTIES(cpuVisible ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT);
	HR(pDevice->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_pResource)));
	m_CurrentState = D3D12_RESOURCE_STATE_GENERIC_READ;
}

void GraphicsBuffer::SetData(CommandContext* pContext, void* pData, uint32 dataSize)
{
	assert(m_Size == dataSize);
	pContext->InitializeBuffer(this, pData, dataSize);
}

void Texture2D::Create(Graphics* pGraphics, CommandContext* pContext, const char* pFilePath)
{
	int components = 0;
	void* pPixels = stbi_load(pFilePath, &m_Width, &m_Height, &components, 4);
	Create(pGraphics, m_Width, m_Height);
	SetData(pContext, pPixels, m_Width * m_Height * 4);
	pContext->ExecuteAndReset(true);
	stbi_image_free(pPixels);
}

void Texture2D::Create(Graphics* pGraphics, int width, int height)
{
	m_Width = width;
	m_Height = height;

	D3D12_RESOURCE_DESC desc = {};
	desc.Alignment = 0;
	desc.DepthOrArraySize = 1;
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Flags = D3D12_RESOURCE_FLAG_NONE;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Width = width;
	desc.Height = height;

	D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

	HR(pGraphics->GetDevice()->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&m_pResource)));
	m_CurrentState = D3D12_RESOURCE_STATE_GENERIC_READ;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.PlaneSlice = 0;
	srvDesc.Texture2D.ResourceMinLODClamp = 0;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;

	m_Srv = pGraphics->AllocateCpuDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	pGraphics->GetDevice()->CreateShaderResourceView(m_pResource, &srvDesc, m_Srv);
}

void Texture2D::SetData(CommandContext* pContext, void* pData, uint32 dataSize)
{
	assert((uint32)(m_Width * m_Height * 4) == dataSize);
	pContext->InitializeTexture(this, pData, dataSize);
}

void Texture2D::CreateForSwapchain(Graphics* pGraphics, ID3D12Resource* pTexture)
{
	m_pResource = pTexture;
	m_CurrentState = D3D12_RESOURCE_STATE_PRESENT;
	D3D12_RESOURCE_DESC desc = pTexture->GetDesc();
	m_Width = desc.Width;
	m_Height = desc.Height;
	m_Rtv = pGraphics->AllocateCpuDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	pGraphics->GetDevice()->CreateRenderTargetView(pTexture, nullptr, m_Rtv);
}

void Texture2D::CreateDepthStencil(Graphics* pGraphics, int width, int height, DXGI_FORMAT format)
{
	m_Width = width;
	m_Height = height;

	D3D12_CLEAR_VALUE clearValue;
	clearValue.Format = format;
	clearValue.DepthStencil.Depth = 1.0f;
	clearValue.DepthStencil.Stencil = 0;

	D3D12_RESOURCE_DESC desc = {};
	desc.Alignment = 0;
	desc.DepthOrArraySize = 1;
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	desc.Format = format;
	desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Width = width;
	desc.Height = height;

	D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

	HR(pGraphics->GetDevice()->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&clearValue,
		IID_PPV_ARGS(&m_pResource)));
	m_CurrentState = D3D12_RESOURCE_STATE_DEPTH_WRITE;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = GetDepthFormat(format);
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.PlaneSlice = 0;
	srvDesc.Texture2D.ResourceMinLODClamp = 0;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;

	m_Srv = pGraphics->AllocateCpuDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	pGraphics->GetDevice()->CreateShaderResourceView(m_pResource, &srvDesc, m_Srv);

	m_Rtv = pGraphics->AllocateCpuDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	pGraphics->GetDevice()->CreateDepthStencilView(m_pResource, nullptr, m_Rtv);
}

DXGI_FORMAT Texture2D::GetDepthFormat(DXGI_FORMAT format)
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
