#include "stdafx.h"
#include "Graphics.h"
#include <map>
#include <fstream>
#include "CommandAllocatorPool.h"
#include "CommandQueue.h"
#include "CommandContext.h"

#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"
#include <assert.h>
#include "DescriptorAllocator.h"

#define STB_IMAGE_IMPLEMENTATION
#include "External/Stb/stb_image.h"
#include "DynamicResourceAllocator.h"
#include "ImGuiRenderer.h"
#include "External/Imgui/imgui.h"

const uint32 Graphics::FRAME_COUNT;

Graphics::Graphics(uint32 width, uint32 height)
	: m_WindowWidth(width), m_WindowHeight(height)
{
}

Graphics::~Graphics()
{
}

void Graphics::Initialize(WindowHandle window)
{
	InitD3D(window);
	OnResize(m_WindowWidth, m_WindowHeight);

	InitializeAssets();
	return;
}

void Graphics::Update()
{
	WaitForFence(m_FenceValues[m_CurrentBackBufferIndex]);

	m_pImGuiRenderer->NewFrame();
	ImGui::ShowDemoWindow();

	CommandContext* pContext = AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
	ID3D12GraphicsCommandList* pCommandList = pContext->GetCommandList();

	pCommandList->SetPipelineState(m_pPipelineStateObject.Get());
	pCommandList->SetGraphicsRootSignature(m_pRootSignature.Get());

	pContext->SetViewport(m_Viewport);
	pContext->SetScissorRect(m_ScissorRect);

	pContext->InsertResourceBarrier(CD3DX12_RESOURCE_BARRIER::Transition(m_RenderTargets[m_CurrentBackBufferIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET), true);

	pContext->SetRenderTarget(&m_RenderTargetHandles[m_CurrentBackBufferIndex]);
	pContext->SetDepthStencil(&m_DepthStencilHandle);

	Color clearColor = Color(0.1f, 0.1f, 0.1f, 1.0f);
	pContext->ClearRenderTarget(m_RenderTargetHandles[m_CurrentBackBufferIndex], clearColor);
	pContext->ClearDepth(m_DepthStencilHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0);

	struct ConstantBufferData
	{
		Matrix World;
		Matrix WorldViewProjection;
	} Data;
	Matrix proj = XMMatrixPerspectiveFovLH(XM_PIDIV4, (float)m_WindowWidth / m_WindowHeight, 0.1f, 1000);
	Matrix view = XMMatrixLookAtLH(Vector3(0, 5, 0), Vector3(0, 0, 500), Vector3(0, 1, 0));
	Matrix world = XMMatrixRotationRollPitchYaw(0, GameTimer::GameTime(), 0) * XMMatrixTranslation(0, -50, 500);
	Data.World = world;
	Data.WorldViewProjection = world * view * proj;
	pContext->SetDynamicConstantBufferView(0, &Data, sizeof(ConstantBufferData));

	pContext->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pContext->SetVertexBuffer(m_VertexBufferView);
	pContext->SetIndexBuffer(m_IndexBufferView);
	pContext->DrawIndexed(m_IndexCount, 0);

	m_pImGuiRenderer->Render(*pContext);
	
	pContext->InsertResourceBarrier(CD3DX12_RESOURCE_BARRIER::Transition(m_RenderTargets[m_CurrentBackBufferIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));
	m_FenceValues[m_CurrentBackBufferIndex] = pContext->Execute(false);

	m_pSwapchain->Present(1, 0);
	m_CurrentBackBufferIndex = m_pSwapchain->GetCurrentBackBufferIndex();
}

void Graphics::CreateDescriptorHeaps()
{
	assert(m_DescriptorHeaps.size() == D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES);
	for (size_t i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
	{
		m_DescriptorHeaps[i] = std::make_unique<DescriptorAllocator>(m_pDevice.Get(), (D3D12_DESCRIPTOR_HEAP_TYPE)i);
	}
}

void Graphics::Shutdown()
{
	// Wait for the GPU to be done with all resources.
	IdleGPU();
}

void Graphics::InitD3D(WindowHandle pWindow)
{
	UINT dxgiFactoryFlags = 0;

#ifdef _DEBUG
	//Enable debug
	ComPtr<ID3D12Debug> pDebugController;
	HR(D3D12GetDebugInterface(IID_PPV_ARGS(&pDebugController)));
	pDebugController->EnableDebugLayer();
	// Enable additional debug layers.
	dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif

	//Create the factory
	HR(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&m_pFactory)));

	//Create the device
	HR(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_pDevice)));

	//Check 4x MSAA support
	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS qualityLevels;
	qualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	qualityLevels.Format = m_RenderTargetFormat;
	qualityLevels.NumQualityLevels = 0;
	qualityLevels.SampleCount = 4;
	HR(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &qualityLevels, sizeof(qualityLevels)));

	m_MsaaQuality = qualityLevels.NumQualityLevels;
	if (m_MsaaQuality <= 0)
	{
		return;
	}

	m_CommandQueues[0] = std::make_unique<CommandQueue>(this, D3D12_COMMAND_LIST_TYPE_DIRECT);

	CreateSwapchain(pWindow);
	CreateDescriptorHeaps();

	m_pDynamicCpuVisibleAllocator = std::make_unique<DynamicResourceAllocator>(m_pDevice.Get(), true, 1024 * 1024);
	m_pImGuiRenderer = std::make_unique<ImGuiRenderer>(this);
}

void Graphics::CreateSwapchain(WindowHandle pWindow)
{
	m_pSwapchain.Reset();

	DXGI_SWAP_CHAIN_DESC1 swapchainDesc = {};
	swapchainDesc.Width = m_WindowWidth;
	swapchainDesc.Height = m_WindowHeight;
	swapchainDesc.Format = m_RenderTargetFormat;
	swapchainDesc.SampleDesc.Count = 1;
	swapchainDesc.SampleDesc.Quality = 0;
	swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapchainDesc.BufferCount = FRAME_COUNT;
	swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapchainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	swapchainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
	swapchainDesc.Stereo = false;
	ComPtr<IDXGISwapChain1> swapChain;
#ifdef PLATFORM_UWP
	HR(m_pFactory->CreateSwapChainForCoreWindow(
		m_CommandQueues[D3D12_COMMAND_LIST_TYPE_DIRECT]->GetCommandQueue(),
		reinterpret_cast<IUnknown*>(pWindow),
		&swapchainDesc,
		nullptr,
		&swapChain));
#else
	DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsDesc{};
	fsDesc.RefreshRate.Denominator = 60;
	fsDesc.RefreshRate.Numerator = 1;
	fsDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	fsDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	fsDesc.Windowed = true;
	HR(m_pFactory->CreateSwapChainForHwnd(m_CommandQueues[D3D12_COMMAND_LIST_TYPE_DIRECT]->GetCommandQueue(), pWindow, &swapchainDesc, &fsDesc, nullptr, &swapChain));
#endif
	swapChain.As(&m_pSwapchain);
}

void Graphics::OnResize(int width, int height)
{
	m_WindowWidth = width;
	m_WindowHeight = height;

	IdleGPU();

	for (int i = 0; i < FRAME_COUNT; ++i)
	{
		m_RenderTargets[i].Reset();
	}
	m_pDepthStencilBuffer.Reset();

	//Resize the buffers
	HR(m_pSwapchain->ResizeBuffers(
		FRAME_COUNT, 
		m_WindowWidth, 
		m_WindowHeight, 
		m_RenderTargetFormat,
		DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));

	m_CurrentBackBufferIndex = 0;

	//Recreate the render target views
	for (int i = 0; i < FRAME_COUNT; ++i)
	{
		m_RenderTargetHandles[i] = m_DescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_RTV]->AllocateDescriptor();
		HR(m_pSwapchain->GetBuffer(i, IID_PPV_ARGS(&m_RenderTargets[i])));
		m_pDevice->CreateRenderTargetView(m_RenderTargets[i].Get(), nullptr, m_RenderTargetHandles[i]);
	}

	//Recreate the depth stencil buffer and view
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Alignment = 0;
	desc.Width = m_WindowWidth;
	desc.Height = m_WindowHeight;
	desc.DepthOrArraySize = 1;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	desc.Format = m_DepthStencilFormat;
	desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;

	D3D12_CLEAR_VALUE clearValue;
	clearValue.Format = m_DepthStencilFormat;
	clearValue.DepthStencil.Depth = 1.0f;
	clearValue.DepthStencil.Stencil = 0;
	D3D12_HEAP_PROPERTIES heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	HR(m_pDevice->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&clearValue,
		IID_PPV_ARGS(&m_pDepthStencilBuffer)));
	m_DepthStencilHandle = m_DescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_DSV]->AllocateDescriptor();
	m_pDevice->CreateDepthStencilView(m_pDepthStencilBuffer.Get(), nullptr, m_DepthStencilHandle);

	m_Viewport.height = m_WindowHeight;
	m_Viewport.width = m_WindowWidth;
	m_Viewport.x = 0;
	m_Viewport.y = 0;
	
	m_ScissorRect.x = 0;
	m_ScissorRect.y = 0;
	m_ScissorRect.width = m_WindowWidth;
	m_ScissorRect.height = m_WindowHeight;
}

void Graphics::InitializeAssets()
{
	BuildRootSignature();
	BuildShadersAndInputLayout();
	BuildGeometry();
	LoadTexture();
	BuildPSO();
}

void Graphics::BuildRootSignature()
{
	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC desc = {};

	CD3DX12_ROOT_PARAMETER1 rootParameters[1];
	rootParameters[0].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_VERTEX);

	D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

	D3D12_STATIC_SAMPLER_DESC samplerDesc = CD3DX12_STATIC_SAMPLER_DESC(0);
	desc.Init_1_1(1, rootParameters, 1, &samplerDesc, rootSignatureFlags);
	
	ComPtr<ID3DBlob> pDataBlob, pErrorBlob;
	HR(D3D12SerializeVersionedRootSignature(&desc, pDataBlob.GetAddressOf(), pErrorBlob.GetAddressOf()));
	HR(m_pDevice->CreateRootSignature(0, pDataBlob->GetBufferPointer(), pDataBlob->GetBufferSize(), IID_PPV_ARGS(m_pRootSignature.GetAddressOf())));
}

void Graphics::BuildShadersAndInputLayout()
{
#if defined(_DEBUG)
	// Enable better shader debugging with the graphics debugging tools.
	uint32 compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
	uint32 compileFlags = 0;
#endif
	compileFlags |= D3DCOMPILE_PACK_MATRIX_ROW_MAJOR;

	std::ifstream file("Resources/shaders.hlsl", std::ios::ate);
	if (file.fail())
	{
		static int a = 0;
	}
	int size = (int)file.tellg();
	std::vector<char> data(size);
	file.seekg(0);
	file.read(data.data(), data.size());
	file.close();

	ComPtr<ID3DBlob> pErrorBlob;
	D3DCompile2(data.data(), data.size(), nullptr, nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, 0, nullptr, 0, m_pVertexShaderCode.GetAddressOf(), pErrorBlob.GetAddressOf());
	if (pErrorBlob != nullptr)
	{
		std::wstring errorMsg = std::wstring((char*)pErrorBlob->GetBufferPointer(), (char*)pErrorBlob->GetBufferPointer() + pErrorBlob->GetBufferSize());
		std::wcout << errorMsg << std::endl;
		return;
	}
	pErrorBlob.Reset();

	D3DCompile2(data.data(), data.size(), nullptr, nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, 0, nullptr, 0, m_pPixelShaderCode.GetAddressOf(), pErrorBlob.GetAddressOf());
	if (pErrorBlob != nullptr)
	{
		std::wstring errorMsg = std::wstring((char*)pErrorBlob->GetBufferPointer(), (char*)pErrorBlob->GetBufferPointer() + pErrorBlob->GetBufferSize());
		std::wcout << errorMsg << std::endl;
		return;
	}

	//Input layout
	m_InputElements.push_back(D3D12_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
	m_InputElements.push_back(D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
	m_InputElements.push_back(D3D12_INPUT_ELEMENT_DESC{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
}

void Graphics::BuildGeometry()
{
	struct Vertex
	{
		Vector3 Position;
		Vector2 TexCoord;
		Vector3 Normal;
	};

	Assimp::Importer importer;
	const aiScene* pScene = importer.ReadFile("Resources/Man.dae",
		aiProcess_Triangulate |
		aiProcess_ConvertToLeftHanded |
		aiProcess_GenSmoothNormals |
		aiProcess_CalcTangentSpace |
		aiProcess_LimitBoneWeights
	);

	std::vector<Vertex> vertices(pScene->mMeshes[0]->mNumVertices);
	for (size_t i = 0; i < vertices.size(); ++i)
	{
		Vertex& vertex = vertices[i];
		vertex.Position = *reinterpret_cast<Vector3*>(&pScene->mMeshes[0]->mVertices[i]);
		vertex.TexCoord = *reinterpret_cast<Vector2*>(&pScene->mMeshes[0]->mTextureCoords[0][i]);
		vertex.Normal = *reinterpret_cast<Vector3*>(&pScene->mMeshes[0]->mNormals[i]);
	}

	std::vector<uint32> indices(pScene->mMeshes[0]->mNumFaces * 3);
	for (size_t i = 0; i < pScene->mMeshes[0]->mNumFaces; ++i)
	{
		for (size_t j = 0; j < 3; ++j)
		{
			assert(pScene->mMeshes[0]->mFaces[i].mNumIndices == 3);
			indices[i * 3 + j] = pScene->mMeshes[0]->mFaces[i].mIndices[j];
		}
	}

	CommandContext* pContext = AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
	ID3D12GraphicsCommandList* pCommandList = pContext->GetCommandList();

	ComPtr<ID3D12Resource> pIndexUploadBuffer;

	{
		uint32 size = vertices.size() * sizeof(Vertex);
		D3D12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(size);
		m_pDevice->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&resourceDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(m_pVertexBuffer.GetAddressOf()));
		pContext->InitializeBuffer(m_pVertexBuffer.Get(), vertices.data(), size);

		m_VertexBufferView.BufferLocation = m_pVertexBuffer->GetGPUVirtualAddress();
		m_VertexBufferView.SizeInBytes = sizeof(Vertex) * (uint32)vertices.size();
		m_VertexBufferView.StrideInBytes = sizeof(Vertex);
	}

	{
		m_IndexCount = (int)indices.size();
		uint32 size = indices.size() * sizeof(uint32);
		D3D12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(size);
		m_pDevice->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&resourceDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(m_pIndexBuffer.GetAddressOf()));
		pContext->InitializeBuffer(m_pIndexBuffer.Get(), indices.data(), size);

		m_IndexBufferView.BufferLocation = m_pIndexBuffer->GetGPUVirtualAddress();
		m_IndexBufferView.SizeInBytes = size;
		m_IndexBufferView.Format = DXGI_FORMAT_R32_UINT;
	}

	pContext->Execute(true);
}

void Graphics::LoadTexture()
{
	std::ifstream file("Resources/Man.png", std::ios::ate | std::ios::binary);
	std::vector<char> buffer((size_t)file.tellg());
	file.seekg(0);
	file.read(buffer.data(), buffer.size());

	int width, height, components;
	void* pPixels = stbi_load_from_memory((unsigned char*)buffer.data(), (int)buffer.size(), &width, &height, &components, 4);

	HR(m_pDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), 
		D3D12_HEAP_FLAG_NONE, 
		&CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height), 
		D3D12_RESOURCE_STATE_COPY_DEST, 
		nullptr,
		IID_PPV_ARGS(m_pTexture.GetAddressOf())));

	ComPtr<ID3D12Resource> pUploadBuffer;

	// In order to copy CPU memory data into our default buffer, we need to create
	// an intermediate upload heap. 
	HR(m_pDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(width * height * components),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(pUploadBuffer.GetAddressOf())));

	// Describe the data we want to copy into the default buffer.
	D3D12_SUBRESOURCE_DATA subResourceData = {};
	subResourceData.pData = pPixels;
	subResourceData.RowPitch = width * height * components;
	subResourceData.SlicePitch = subResourceData.RowPitch;

	CommandContext* pContext = AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
	ID3D12GraphicsCommandList* pCmd = pContext->GetCommandList();

	// Schedule to copy the data to the default buffer resource.  At a high level, the helper function UpdateSubresources
	// will copy the CPU memory into the intermediate upload heap.  Then, using ID3D12CommandList::CopySubresourceRegion,
	// the intermediate upload heap data will be copied to mBuffer.
	UpdateSubresources<1>(pCmd, m_pTexture.Get(), pUploadBuffer.Get(), 0, 0, 1, &subResourceData);
	pCmd->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_pTexture.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ));

	pContext->Execute(true);

	stbi_image_free(pPixels);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.PlaneSlice = 0;
	srvDesc.Texture2D.ResourceMinLODClamp = 0;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;

	m_TextureHandle = m_DescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]->AllocateDescriptor();
	m_pDevice->CreateShaderResourceView(m_pTexture.Get(), &srvDesc, m_TextureHandle);
}

void Graphics::BuildPSO()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
	desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	desc.DSVFormat = m_DepthStencilFormat;
	desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	desc.InputLayout.NumElements = (uint32)m_InputElements.size();
	desc.InputLayout.pInputElementDescs = m_InputElements.data();
	desc.NodeMask = 0;
	desc.NumRenderTargets = 1;
	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	desc.pRootSignature = m_pRootSignature.Get();
	desc.PS = CD3DX12_SHADER_BYTECODE(m_pPixelShaderCode.Get());
	desc.VS = CD3DX12_SHADER_BYTECODE(m_pVertexShaderCode.Get());
	desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	desc.RTVFormats[0] = m_RenderTargetFormat;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.SampleMask = UINT_MAX;
	HR(m_pDevice->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(m_pPipelineStateObject.GetAddressOf())));
}

CommandQueue* Graphics::GetCommandQueue(D3D12_COMMAND_LIST_TYPE type) const
{
	return m_CommandQueues.at(type).get();
}

CommandContext* Graphics::AllocateCommandContext(D3D12_COMMAND_LIST_TYPE type)
{
	if (m_FreeCommandLists.size() > 0)
	{
		CommandContext* pCommandList = m_FreeCommandLists.front();
		m_FreeCommandLists.pop();
		pCommandList->Reset();
		return pCommandList;
	}
	else
	{
		ComPtr<ID3D12CommandList> pCommandList;
		ID3D12CommandAllocator* pAllocator = m_CommandQueues[type]->RequestAllocator();
		m_pDevice->CreateCommandList(0, type, pAllocator, nullptr, IID_PPV_ARGS(pCommandList.GetAddressOf()));
		m_CommandLists.push_back(std::move(pCommandList));
		m_CommandListPool.emplace_back(std::make_unique<CommandContext>(this, static_cast<ID3D12GraphicsCommandList*>(m_CommandLists.back().Get()), pAllocator, type));
		return m_CommandListPool.back().get();
	}
}

void Graphics::WaitForFence(uint64 fenceValue)
{
	D3D12_COMMAND_LIST_TYPE type = (D3D12_COMMAND_LIST_TYPE)(fenceValue >> 56);
	CommandQueue* pQueue = GetCommandQueue(type);
	pQueue->WaitForFence(fenceValue);
}

void Graphics::FreeCommandList(CommandContext* pCommandList)
{
	m_FreeCommandLists.push(pCommandList);
}

void Graphics::IdleGPU()
{
	for (auto& pCommandQueue : m_CommandQueues)
	{
		pCommandQueue->WaitForIdle();
	}
}
