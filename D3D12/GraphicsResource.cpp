#include "stdafx.h"
#include "GraphicsResource.h"
#include "CommandContext.h"
#include "External/Stb/stb_image.h"
#include "Graphics.h"

void GraphicsBuffer::Create(ID3D12Device* pDevice, int size, bool cpuVisible /*= false*/)
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

void GraphicsBuffer::SetData(CommandContext* pContext, void* pData, int dataSize)
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

	HR(pGraphics->GetDevice()->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
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

	m_DescriptorHandle = pGraphics->AllocateCpuDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	pGraphics->GetDevice()->CreateShaderResourceView(m_pResource, &srvDesc, m_DescriptorHandle);
}

void Texture2D::SetData(CommandContext* pContext, void* pData, int dataSize)
{
	assert(m_Width * m_Height * 4 == dataSize);
	pContext->InitializeTexture(this, pData, dataSize);
}
