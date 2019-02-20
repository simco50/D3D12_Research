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
#include "GraphicsResource.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "Shader.h"
#include "Mesh.h"

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
	InitializeAssets();
	return;
}

void Graphics::Update()
{
	WaitForFence(m_FenceValues[m_CurrentBackBufferIndex]);

	m_pImGuiRenderer->NewFrame();
	ImGui::Text("Hello World");

	//3D
	{
		CommandContext* pContext = AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
		ID3D12GraphicsCommandList* pCommandList = pContext->GetCommandList();

		pCommandList->SetPipelineState(m_pPipelineStateObject->GetPipelineState());
		pCommandList->SetGraphicsRootSignature(m_pRootSignature->GetRootSignature());

		pContext->SetViewport(m_Viewport);
		pContext->SetScissorRect(m_ScissorRect);

		pContext->InsertResourceBarrier(m_RenderTargets[m_CurrentBackBufferIndex].get(), D3D12_RESOURCE_STATE_RENDER_TARGET, true);

		pContext->SetRenderTarget(&m_RenderTargetHandles[m_CurrentBackBufferIndex]);
		pContext->SetDepthStencil(&m_DepthStencilHandle);

		Color clearColor = Color(0.1f, 0.1f, 0.1f, 1.0f);
		pContext->ClearRenderTarget(m_RenderTargetHandles[m_CurrentBackBufferIndex], clearColor);
		pContext->ClearDepth(m_DepthStencilHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0);

		ID3D12DescriptorHeap* pHeap = m_pTextureGpuDesciptorHeap->GetCurrentHeap();
		pContext->GetCommandList()->SetDescriptorHeaps(1, &pHeap);
		pContext->GetCommandList()->SetGraphicsRootDescriptorTable(1, m_pTextureGpuDesciptorHeap->GetCurrentHeap()->GetGPUDescriptorHandleForHeapStart());
		pContext->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		{
			struct ConstantBufferData
			{
				Matrix World;
				Matrix WorldViewProjection;
			} Data;
			Matrix proj = XMMatrixPerspectiveFovLH(XM_PIDIV4, (float)m_WindowWidth / m_WindowHeight, 0.1f, 1000);
			Matrix view = XMMatrixLookAtLH(Vector3(0, 5, 0), Vector3(0, 0, 500), Vector3(0, 1, 0));
			Matrix world = XMMatrixRotationRollPitchYaw(0, GameTimer::GameTime(), 0) * XMMatrixTranslation(-50, -50, 500);
			Data.World = world;
			Data.WorldViewProjection = world * view * proj;
			pContext->SetDynamicConstantBufferView(0, &Data, sizeof(ConstantBufferData));
			m_pMesh->Draw(pContext);
		}
		{
			struct ConstantBufferData
			{
				Matrix World;
				Matrix WorldViewProjection;
			} Data;
			Matrix proj = XMMatrixPerspectiveFovLH(XM_PIDIV4, (float)m_WindowWidth / m_WindowHeight, 0.1f, 1000);
			Matrix view = XMMatrixLookAtLH(Vector3(0, 5, 0), Vector3(0, 0, 500), Vector3(0, 1, 0));
			Matrix world = XMMatrixRotationRollPitchYaw(0, -GameTimer::GameTime(), 0) * XMMatrixTranslation(50, -50, 500);
			Data.World = world;
			Data.WorldViewProjection = world * view * proj;
			pContext->SetDynamicConstantBufferView(0, &Data, sizeof(ConstantBufferData));
			m_pMesh->Draw(pContext);
		}

		pContext->Execute(false);
	}

	//UI
	{
		CommandContext* pContext = AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
		m_pImGuiRenderer->Render(*pContext);
		pContext->InsertResourceBarrier(m_RenderTargets[m_CurrentBackBufferIndex].get(), D3D12_RESOURCE_STATE_PRESENT, true);
		m_FenceValues[m_CurrentBackBufferIndex] = pContext->Execute(false);
	}

	m_pSwapchain->Present(1, 0);
	m_CurrentBackBufferIndex = m_pSwapchain->GetCurrentBackBufferIndex();
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

	m_CommandQueues[0] = std::make_unique<CommandQueue>(this, D3D12_COMMAND_LIST_TYPE_DIRECT);

	assert(m_DescriptorHeaps.size() == D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES);
	for (size_t i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
	{
		m_DescriptorHeaps[i] = std::make_unique<DescriptorAllocator>(m_pDevice.Get(), (D3D12_DESCRIPTOR_HEAP_TYPE)i);
	}
	m_pTextureGpuDesciptorHeap = std::make_unique<DescriptorAllocator>(m_pDevice.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);

	m_pDynamicCpuVisibleAllocator = std::make_unique<DynamicResourceAllocator>(m_pDevice.Get(), true, 1024 * 1024 * 32);

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
	OnResize(m_WindowWidth, m_WindowHeight);

	m_pImGuiRenderer = std::make_unique<ImGuiRenderer>(this);
}

void Graphics::OnResize(int width, int height)
{
	m_WindowWidth = width;
	m_WindowHeight = height;

	IdleGPU();
	for (int i = 0; i < FRAME_COUNT; ++i)
	{
		m_RenderTargets[i].reset();
	}
	m_pDepthStencilBuffer.reset();

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
		ID3D12Resource* pResource = nullptr;
		m_RenderTargetHandles[i] = m_DescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_RTV]->AllocateDescriptor();
		HR(m_pSwapchain->GetBuffer(i, IID_PPV_ARGS(&pResource)));
		m_pDevice->CreateRenderTargetView(pResource, nullptr, m_RenderTargetHandles[i]);
		m_RenderTargets[i] = std::make_unique<GraphicsResource>(pResource, D3D12_RESOURCE_STATE_PRESENT);
	}

	//Recreate the depth stencil buffer and view
	D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(m_DepthStencilFormat, width, height, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
	ID3D12Resource* pResource = nullptr;
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
		IID_PPV_ARGS(&pResource)));
	m_DepthStencilHandle = m_DescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_DSV]->AllocateDescriptor();
	m_pDevice->CreateDepthStencilView(pResource, nullptr, m_DepthStencilHandle);
	m_pDepthStencilBuffer = std::make_unique<GraphicsResource>(pResource, D3D12_RESOURCE_STATE_DEPTH_WRITE);

	m_Viewport.Bottom = (float)m_WindowHeight;
	m_Viewport.Right = (float)m_WindowWidth;
	m_Viewport.Left = 0.0f;
	m_Viewport.Top = 0.0f;
	m_ScissorRect = m_Viewport;
}

void Graphics::InitializeAssets()
{
	CreatePipeline();
	LoadGeometry();
	LoadTexture();
}

void Graphics::CreatePipeline()
{
	//Shaders
	Shader vertexShader;
	vertexShader.Load("Resources/shaders.hlsl", Shader::Type::VertexShader, "VSMain");
	Shader pixelShader;
	pixelShader.Load("Resources/shaders.hlsl", Shader::Type::PixelShader, "PSMain");

	//Input layout
	D3D12_INPUT_ELEMENT_DESC inputElements[] = {
		D3D12_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		D3D12_INPUT_ELEMENT_DESC{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	//Rootsignature
	m_pRootSignature = std::make_unique<RootSignature>(2);
	(*m_pRootSignature)[0].AsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
	(*m_pRootSignature)[1].AsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, D3D12_SHADER_VISIBILITY_PIXEL);

	D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

	D3D12_SAMPLER_DESC samplerDesc = {};
	samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	m_pRootSignature->AddStaticSampler(0, samplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);
	m_pRootSignature->Finalize(m_pDevice.Get(), rootSignatureFlags);

	//Pipeline state
	m_pPipelineStateObject = std::make_unique<PipelineState>();
	m_pPipelineStateObject->SetInputLayout(inputElements, sizeof(inputElements) / sizeof(inputElements[0]));
	m_pPipelineStateObject->SetRootSignature(m_pRootSignature->GetRootSignature());
	m_pPipelineStateObject->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
	m_pPipelineStateObject->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
	m_pPipelineStateObject->Finalize(m_pDevice.Get());
}

void Graphics::LoadGeometry()
{
	CommandContext* pContext = AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
	m_pMesh = std::make_unique<Mesh>();
	m_pMesh->Load("Resources/Man.dae", m_pDevice.Get(), pContext);
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

	m_pTexture = std::make_unique<Texture2D>();
	m_pTexture->Create(m_pDevice.Get(), width, height);

	CommandContext* pContext = AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
	m_pTexture->SetData(pContext, pPixels, width * height * 4);
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

	m_TextureHandle = m_pTextureGpuDesciptorHeap->AllocateDescriptor();
	m_pDevice->CreateShaderResourceView(m_pTexture->GetResource(), &srvDesc, m_TextureHandle);
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
