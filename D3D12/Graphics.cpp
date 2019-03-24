#include "stdafx.h"
#include "Graphics.h"
#include <map>
#include <fstream>
#include "CommandAllocatorPool.h"
#include "CommandQueue.h"
#include "CommandContext.h"

#include "DescriptorAllocator.h"
#include "GraphicsResource.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "Shader.h"
#include "Mesh.h"
#include "DynamicResourceAllocator.h"
#include "ImGuiRenderer.h"
#include "External/Imgui/imgui.h"
#include "Input.h"

const DXGI_FORMAT Graphics::DEPTH_STENCIL_FORMAT = DXGI_FORMAT_D24_UNORM_S8_UINT;
const DXGI_FORMAT Graphics::RENDER_TARGET_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;

Graphics::Graphics(uint32 width, uint32 height, int sampleCount /*= 1*/)
	: m_WindowWidth(width), m_WindowHeight(height), m_SampleCount(sampleCount)
{

}

Graphics::~Graphics()
{
}

void Graphics::Initialize(HWND window)
{
	m_pWindow = window;
	InitD3D();
	InitializeAssets();

	m_FrameTimes.resize(60*3);

	m_CameraPosition = Vector3(0, 100, -15);
	m_CameraRotation = Quaternion::CreateFromYawPitchRoll(XM_PIDIV4, XM_PIDIV4, 0);

	m_Lights.resize(2048);
	for (int i = 0; i < m_Lights.size(); ++i)
	{
		Vector3 c = Vector3(Math::RandomRange(0, 1), Math::RandomRange(0, 1), Math::RandomRange(0, 1));
		Vector4 color(c.x, c.y, c.z, 1);
		int type = rand() % 2;
		if (type == 0)
		{
			m_Lights[i] = Light::Point(Vector3(Math::RandomRange(-140, 140), Math::RandomRange(0, 150), Math::RandomRange(-60, 60)), 15.0f, 1.0f, 0.5f, color);
		}
		else
		{
			m_Lights[i] = Light::Cone(Vector3(Math::RandomRange(-140, 140), Math::RandomRange(20, 150), Math::RandomRange(-60, 60)), 25.0f, Math::RandVector(), 45.0f, 1.0f, 0.5f, color);
		}
	}
}

void Graphics::Update()
{
	if (Input::Instance().IsKeyPressed('P'))
	{
		m_UseDebugView = !m_UseDebugView;
	}

	struct PerFrameData
	{
		Matrix LightViewProjection;
		Matrix ViewInverse;
	} frameData;

	for (Light& l : m_Lights)
	{
		l.Position += Vector3::Down * GameTimer::DeltaTime() * 5;
		if (l.Position.y < 0)
		{
			l.Position.y = 150;
		}
	}

	Vector3 mainLightPosition = Vector3(cos((float)GameTimer::GameTime() / 5.0f), 1.5, sin((float)GameTimer::GameTime() / 5.0f)) * 80;
	Vector3 mainLightDirection;
	mainLightPosition.Normalize(mainLightDirection);
	mainLightDirection *= -1;
	m_Lights[0] = Light::Directional(mainLightPosition, mainLightDirection);

	frameData.LightViewProjection = XMMatrixLookAtLH(m_Lights[0].Position, Vector3(0, 0, 0), Vector3(0, 1, 0)) * XMMatrixOrthographicLH(512, 512, 5.0f, 200.0f);

	if (Input::Instance().IsMouseDown(0))
	{
		Vector2 mouseDelta = Input::Instance().GetMouseDelta();
		Quaternion yr = Quaternion::CreateFromYawPitchRoll(0, mouseDelta.y * GameTimer::DeltaTime() * 0.1f, 0);
		Quaternion pr = Quaternion::CreateFromYawPitchRoll(mouseDelta.x * GameTimer::DeltaTime() * 0.1f, 0, 0);
		m_CameraRotation = yr * m_CameraRotation * pr;
	}

	Vector3 movement;
	movement.x -= (int)Input::Instance().IsKeyDown('A');
	movement.x += (int)Input::Instance().IsKeyDown('D');
	movement.z -= (int)Input::Instance().IsKeyDown('S');
	movement.z += (int)Input::Instance().IsKeyDown('W');
	movement = Vector3::Transform(movement, m_CameraRotation);
	movement.y -= (int)Input::Instance().IsKeyDown('Q');
	movement.y += (int)Input::Instance().IsKeyDown('E');
	movement *= GameTimer::DeltaTime() * 20.0f;
	m_CameraPosition += movement;

	frameData.ViewInverse = Matrix::CreateFromQuaternion(m_CameraRotation) * Matrix::CreateTranslation(m_CameraPosition);
	Matrix cameraView;
	frameData.ViewInverse.Invert(cameraView);
	Matrix cameraProjection = XMMatrixPerspectiveFovLH(XM_PIDIV4, (float)m_WindowWidth / m_WindowHeight, 1.0f, 300);
	Matrix cameraViewProjection = cameraView * cameraProjection;

	BeginFrame();

	uint64 nextFenceValue = 0;
	uint64 lightCullingFence = 0;
	uint64 shadowsFence = 0;
	uint64 depthPrepassFence = 0;

	//Depth prepass
	{
		GraphicsCommandContext* pContext = (GraphicsCommandContext*)AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
		pContext->MarkBegin(L"Depth Prepass");
		pContext->SetPipelineState(m_pDepthPrepassPipelineStateObject.get());
		pContext->SetGraphicsRootSignature(m_pDepthPrepassRootSignature.get());

		pContext->SetViewport(FloatRect(0, 0, (float)m_WindowWidth, (float)m_WindowHeight));
		pContext->SetScissorRect(FloatRect(0, 0, (float)m_WindowWidth, (float)m_WindowHeight));

		pContext->InsertResourceBarrier(GetDepthStencil(), D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
		pContext->SetDepthOnlyTarget(GetDepthStencil()->GetRTV());

		Color clearColor = Color(0.1f, 0.1f, 0.1f, 1.0f);
		pContext->ClearDepth(GetDepthStencil()->GetRTV(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0);

		pContext->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		struct PerObjectData
		{
			Matrix WorldViewProjection;
		} ObjectData;
		ObjectData.WorldViewProjection = cameraViewProjection;
		pContext->SetDynamicConstantBufferView(0, &ObjectData, sizeof(PerObjectData));
		for (int i = 0; i < m_pMesh->GetMeshCount(); ++i)
		{
			m_pMesh->GetMesh(i)->Draw(pContext);
		}

		if (m_SampleCount > 1)
		{
			pContext->InsertResourceBarrier(GetResolvedDepthStencil(), D3D12_RESOURCE_STATE_RESOLVE_DEST, false);
			pContext->InsertResourceBarrier(GetDepthStencil(), D3D12_RESOURCE_STATE_RESOLVE_SOURCE, true);
			pContext->GetCommandList()->ResolveSubresource(GetResolvedDepthStencil()->GetResource(), 0, GetDepthStencil()->GetResource(), 0, DXGI_FORMAT_R24_UNORM_X8_TYPELESS);
		}
		pContext->InsertResourceBarrier(GetDepthStencil(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, false);

		pContext->MarkEnd();
		depthPrepassFence = pContext->Execute(false);
	}

	m_CommandQueues[D3D12_COMMAND_LIST_TYPE_COMPUTE]->InsertWaitForFence(depthPrepassFence);

	//Light culling
	{
		ComputeCommandContext* pContext = (ComputeCommandContext*)AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_COMPUTE);
		pContext->MarkBegin(L"Light Culling");

		pContext->MarkBegin(L"Setup Light Data");
		uint32 zero = 0;
		m_pLightIndexCounterBuffer->SetData(pContext, &zero, sizeof(uint32));
		m_pLightBuffer->SetData(pContext, m_Lights.data(), m_Lights.size() * sizeof(Light));
		pContext->MarkEnd();

		pContext->MarkBegin(L"Light Culling");
		pContext->SetPipelineState(m_pComputeLightCullPipeline.get());
		pContext->SetComputeRootSignature(m_pComputeLightCullRootSignature.get());


#pragma pack(push)
#pragma pack(16) 
		struct ShaderParameters
		{
			Matrix CameraView;
			uint32 NumThreadGroups[4];
			Matrix ProjectionInverse;
			Vector2 ScreenDimensions;
		} Data;
#pragma pack(pop)

		Data.CameraView = cameraView;
		Data.NumThreadGroups[0] = Math::RoundUp((float)m_WindowWidth / FORWARD_PLUS_BLOCK_SIZE);
		Data.NumThreadGroups[1] = Math::RoundUp((float)m_WindowHeight / FORWARD_PLUS_BLOCK_SIZE);
		Data.NumThreadGroups[2] = 1;
		Data.ScreenDimensions.x = (float)m_WindowWidth;
		Data.ScreenDimensions.y = (float)m_WindowHeight;
		cameraProjection.Invert(Data.ProjectionInverse);

		pContext->SetDynamicConstantBufferView(0, &Data, sizeof(ShaderParameters));
		pContext->SetDynamicDescriptor(1, 0, m_pLightIndexCounterBuffer->GetUAV());
		pContext->SetDynamicDescriptor(1, 1, m_pLightIndexListBuffer->GetUAV());
		pContext->SetDynamicDescriptor(1, 2, m_pLightGrid->GetUAV());
		pContext->SetDynamicDescriptor(2, 0, GetResolvedDepthStencil()->GetSRV());
		pContext->SetDynamicDescriptor(2, 1, m_pLightBuffer->GetSRV());

		pContext->Dispatch(Data.NumThreadGroups[0], Data.NumThreadGroups[1], Data.NumThreadGroups[2]);
		pContext->MarkEnd();
		pContext->MarkEnd();
		lightCullingFence = pContext->Execute(false);
	}

	//Shadow Map
	{
		GraphicsCommandContext* pContext = (GraphicsCommandContext*)AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
		pContext->MarkBegin(L"Shadows");
		pContext->SetPipelineState(m_pShadowsPipelineStateObject.get());
		pContext->SetGraphicsRootSignature(m_pShadowsRootSignature.get());

		pContext->SetViewport(FloatRect(0, 0, (float)m_pShadowMap->GetWidth(), (float)m_pShadowMap->GetHeight()));
		pContext->SetScissorRect(FloatRect(0, 0, (float)m_pShadowMap->GetWidth(), (float)m_pShadowMap->GetHeight()));

		pContext->InsertResourceBarrier(m_pShadowMap.get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
		pContext->SetDepthOnlyTarget(m_pShadowMap->GetRTV());

		pContext->ClearDepth(m_pShadowMap->GetRTV(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0);

		pContext->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		struct PerObjectData
		{
			Matrix WorldViewProjection;
		} ObjectData;
		ObjectData.WorldViewProjection = frameData.LightViewProjection;
		pContext->SetDynamicConstantBufferView(0, &ObjectData, sizeof(PerObjectData));
		for (int i = 0; i < m_pMesh->GetMeshCount(); ++i)
		{
			m_pMesh->GetMesh(i)->Draw(pContext);
		}
		pContext->MarkEnd();
		shadowsFence = pContext->Execute(false);
	}

	m_CommandQueues[D3D12_COMMAND_LIST_TYPE_DIRECT]->InsertWaitForFence(lightCullingFence);

	//3D
	{
		GraphicsCommandContext* pContext = (GraphicsCommandContext*)AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
		pContext->MarkBegin(L"3D");
		pContext->SetPipelineState(m_UseDebugView ? m_pPipelineStateObjectDebug.get() : m_pPipelineStateObject.get());
		pContext->SetGraphicsRootSignature(m_pRootSignature.get());
	
		pContext->SetViewport(m_Viewport);
		pContext->SetScissorRect(m_ScissorRect);
	
		pContext->InsertResourceBarrier(m_pShadowMap.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, false);
		pContext->InsertResourceBarrier(m_pLightGrid.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, false);
		pContext->InsertResourceBarrier(m_pLightIndexListBuffer.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, false);
		pContext->InsertResourceBarrier(GetDepthStencil(), D3D12_RESOURCE_STATE_DEPTH_WRITE, false);
		pContext->InsertResourceBarrier(GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_RENDER_TARGET, true);

		pContext->SetRenderTarget(GetCurrentRenderTarget()->GetRTV(), GetDepthStencil()->GetRTV());
	
		Color clearColor = Color(0, 0, 0, 1);
		pContext->ClearRenderTarget(GetCurrentRenderTarget()->GetRTV(), clearColor);
	
		pContext->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	
		struct PerObjectData
		{
			Matrix World;
			Matrix WorldViewProjection;
		} ObjectData;
		ObjectData.World = XMMatrixIdentity();
		ObjectData.WorldViewProjection = ObjectData.World * cameraViewProjection;
	
		pContext->SetDynamicConstantBufferView(0, &ObjectData, sizeof(PerObjectData));
		pContext->SetDynamicConstantBufferView(1, &frameData, sizeof(PerFrameData));
		pContext->SetDynamicDescriptor(3, 0, m_pShadowMap->GetSRV());
		pContext->SetDynamicDescriptor(3, 1, m_pLightGrid->GetSRV());
		pContext->SetDynamicDescriptor(3, 2, m_pLightIndexListBuffer->GetSRV());
		pContext->SetDynamicDescriptor(3, 3, m_pLightBuffer->GetSRV());
		for (int i = 0; i < m_pMesh->GetMeshCount(); ++i)
		{
			SubMesh* pSubMesh = m_pMesh->GetMesh(i);
			const Material& material = m_pMesh->GetMaterial(pSubMesh->GetMaterialId());
			pContext->SetDynamicDescriptor(2, 0, material.pDiffuseTexture->GetSRV());
			pContext->SetDynamicDescriptor(2, 1, material.pNormalTexture->GetSRV());
			pContext->SetDynamicDescriptor(2, 2, material.pSpecularTexture->GetSRV());
			pSubMesh->Draw(pContext);
		}
		pContext->MarkEnd();
	
		pContext->InsertResourceBarrier(m_pLightGrid.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, false);
		pContext->InsertResourceBarrier(m_pLightIndexListBuffer.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
	
		pContext->Execute(false);
	}


	GraphicsCommandContext* pContext = (GraphicsCommandContext*)AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
	pContext->MarkBegin(L"UI");
	//UI
	{
		UpdateImGui();
		m_pImGuiRenderer->Render(*pContext);
	}
	pContext->MarkEnd();

	pContext->MarkBegin(L"Present");
	//Present
	{
		if (m_SampleCount > 1)
		{
			pContext->InsertResourceBarrier(GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_RESOLVE_SOURCE, false);
			pContext->InsertResourceBarrier(GetCurrentBackbuffer(), D3D12_RESOURCE_STATE_RESOLVE_DEST, true);
			pContext->GetCommandList()->ResolveSubresource(GetCurrentBackbuffer()->GetResource(), 0, GetCurrentRenderTarget()->GetResource(), 0, RENDER_TARGET_FORMAT);
		}
		pContext->InsertResourceBarrier(GetCurrentBackbuffer(), D3D12_RESOURCE_STATE_PRESENT, true);
	}
	pContext->MarkEnd();
	nextFenceValue = pContext->Execute(false);

	EndFrame(nextFenceValue);
}

void Graphics::Shutdown()
{
	// Wait for the GPU to be done with all resources.
	IdleGPU();
}

void Graphics::BeginFrame()
{
	m_pImGuiRenderer->NewFrame();
}

void Graphics::EndFrame(uint64 fenceValue)
{
	m_FenceValues[m_CurrentBackBufferIndex] = fenceValue;
	m_pSwapchain->Present(1, 0);
	m_CurrentBackBufferIndex = m_pSwapchain->GetCurrentBackBufferIndex();
	WaitForFence(m_FenceValues[m_CurrentBackBufferIndex]);
	m_pDynamicCpuVisibleAllocator->ResetAllocationCounter();
}

void Graphics::InitD3D()
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

#ifdef _DEBUG
	ID3D12InfoQueue* pInfoQueue = nullptr;
	if (HR(m_pDevice->QueryInterface(IID_PPV_ARGS(&pInfoQueue))))
	{
		// Suppress whole categories of messages
		//D3D12_MESSAGE_CATEGORY Categories[] = {};

		// Suppress messages based on their severity level
		D3D12_MESSAGE_SEVERITY Severities[] =
		{
			D3D12_MESSAGE_SEVERITY_INFO
		};

		// Suppress individual messages by their ID
		D3D12_MESSAGE_ID DenyIds[] =
		{
			// This occurs when there are uninitialized descriptors in a descriptor table, even when a
			// shader does not access the missing descriptors.  I find this is common when switching
			// shader permutations and not wanting to change much code to reorder resources.
			D3D12_MESSAGE_ID_INVALID_DESCRIPTOR_HANDLE,
		};

		D3D12_INFO_QUEUE_FILTER NewFilter = {};
		//NewFilter.DenyList.NumCategories = _countof(Categories);
		//NewFilter.DenyList.pCategoryList = Categories;
		NewFilter.DenyList.NumSeverities = _countof(Severities);
		NewFilter.DenyList.pSeverityList = Severities;
		NewFilter.DenyList.NumIDs = _countof(DenyIds);
		NewFilter.DenyList.pIDList = DenyIds;

		pInfoQueue->PushStorageFilter(&NewFilter);
		pInfoQueue->Release();
	}
#endif

	//Check 4x MSAA support
	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS qualityLevels;
	qualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	qualityLevels.Format = RENDER_TARGET_FORMAT;
	qualityLevels.NumQualityLevels = 0;
	qualityLevels.SampleCount = m_SampleCount;
	HR(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &qualityLevels, sizeof(qualityLevels)));
	m_SampleQuality = qualityLevels.NumQualityLevels - 1;

	m_CommandQueues[D3D12_COMMAND_LIST_TYPE_DIRECT] = std::make_unique<CommandQueue>(this, D3D12_COMMAND_LIST_TYPE_DIRECT);
	m_CommandQueues[D3D12_COMMAND_LIST_TYPE_COMPUTE] = std::make_unique<CommandQueue>(this, D3D12_COMMAND_LIST_TYPE_COMPUTE);

	assert(m_DescriptorHeaps.size() == D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES);
	for (size_t i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
	{
		m_DescriptorHeaps[i] = std::make_unique<DescriptorAllocator>(m_pDevice.Get(), (D3D12_DESCRIPTOR_HEAP_TYPE)i);
	}
	m_pDynamicCpuVisibleAllocator = std::make_unique<DynamicResourceAllocator>(this, true, 0x400000);

	m_pSwapchain.Reset();

	DXGI_SWAP_CHAIN_DESC1 swapchainDesc = {};
	swapchainDesc.Width = m_WindowWidth;
	swapchainDesc.Height = m_WindowHeight;
	swapchainDesc.Format = RENDER_TARGET_FORMAT;
	swapchainDesc.SampleDesc.Count = 1;
	swapchainDesc.SampleDesc.Quality = 0;
	swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapchainDesc.BufferCount = FRAME_COUNT;
	swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
	swapchainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	swapchainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
	swapchainDesc.Stereo = false;
	ComPtr<IDXGISwapChain1> swapChain;

	DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsDesc{};
	fsDesc.RefreshRate.Denominator = 60;
	fsDesc.RefreshRate.Numerator = 1;
	fsDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	fsDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	fsDesc.Windowed = true;
	HR(m_pFactory->CreateSwapChainForHwnd(
		m_CommandQueues[D3D12_COMMAND_LIST_TYPE_DIRECT]->GetCommandQueue(), 
		m_pWindow, 
		&swapchainDesc, 
		&fsDesc, 
		nullptr, 
		&swapChain));

	swapChain.As(&m_pSwapchain);

	for (int i = 0; i < FRAME_COUNT; ++i)
	{
		m_RenderTargets[i] = std::make_unique<Texture2D>();
		if (m_SampleCount > 1)
		{
			m_MultiSampleRenderTargets[i] = std::make_unique<Texture2D>();
		}
	}
	m_pLightGrid = std::make_unique<Texture2D>();
	m_pDepthStencil = std::make_unique<Texture2D>();
	if (m_SampleCount > 1)
	{
		m_pResolvedDepthStencil = std::make_unique<Texture2D>();
	}

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
		m_RenderTargets[i]->Release();
	}
	m_pDepthStencil->Release();

	//Resize the buffers
	HR(m_pSwapchain->ResizeBuffers(
		FRAME_COUNT, 
		m_WindowWidth, 
		m_WindowHeight, 
		RENDER_TARGET_FORMAT,
		DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));

	m_CurrentBackBufferIndex = 0;

	//Recreate the render target views
	for (int i = 0; i < FRAME_COUNT; ++i)
	{
		ID3D12Resource* pResource = nullptr;
		HR(m_pSwapchain->GetBuffer(i, IID_PPV_ARGS(&pResource)));
		m_RenderTargets[i]->CreateForSwapchain(this, pResource);

		if (m_SampleCount > 1)
		{
			m_MultiSampleRenderTargets[i]->Create(this, width, height, RENDER_TARGET_FORMAT, TextureUsage::RenderTarget, m_SampleCount);
		}
	}
	if (m_SampleCount > 1)
	{
		m_pDepthStencil->Create(this, width, height, DEPTH_STENCIL_FORMAT, TextureUsage::DepthStencil, m_SampleCount);
		m_pResolvedDepthStencil->Create(this, width, height, DEPTH_STENCIL_FORMAT, TextureUsage::DepthStencil | TextureUsage::ShaderResource, 1);
	}
	else
	{
		m_pDepthStencil->Create(this, width, height, DEPTH_STENCIL_FORMAT, TextureUsage::DepthStencil | TextureUsage::ShaderResource, m_SampleCount);
	}

	int frustumCountX = (int)(ceil((float)width / FORWARD_PLUS_BLOCK_SIZE));
	int frustumCountY = (int)(ceil((float)height / FORWARD_PLUS_BLOCK_SIZE));
	m_pLightGrid->Create(this, frustumCountX, frustumCountY, DXGI_FORMAT_R32G32_UINT, TextureUsage::ShaderResource | TextureUsage::UnorderedAccess, 1);

	m_Viewport.Bottom = (float)m_WindowHeight;
	m_Viewport.Right = (float)m_WindowWidth;
	m_Viewport.Left = 0.0f;
	m_Viewport.Top = 0.0f;
	m_ScissorRect = m_Viewport;
}

void Graphics::InitializeAssets()
{
	GraphicsCommandContext* pContext = (GraphicsCommandContext*)AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);

	//Input layout
	D3D12_INPUT_ELEMENT_DESC inputElements[] = {
		D3D12_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		D3D12_INPUT_ELEMENT_DESC{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		D3D12_INPUT_ELEMENT_DESC{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 1, DXGI_FORMAT_R32G32B32_FLOAT, 0, 44, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	{
		//Shaders
		Shader vertexShader;
		vertexShader.Load("Resources/Diffuse.hlsl", Shader::Type::VertexShader, "VSMain");
		Shader pixelShader;
		pixelShader.Load("Resources/Diffuse.hlsl", Shader::Type::PixelShader, "PSMain");

		//Rootsignature
		m_pRootSignature = std::make_unique<RootSignature>(4);
		m_pRootSignature->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
		m_pRootSignature->SetConstantBufferView(1, 1, D3D12_SHADER_VISIBILITY_ALL);
		m_pRootSignature->SetDescriptorTableSimple(2, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, D3D12_SHADER_VISIBILITY_PIXEL);
		m_pRootSignature->SetDescriptorTableSimple(3, 3, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, D3D12_SHADER_VISIBILITY_PIXEL);

		D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

		D3D12_SAMPLER_DESC samplerDesc = {};
		samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
		m_pRootSignature->AddStaticSampler(0, samplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);

		samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
		m_pRootSignature->AddStaticSampler(1, samplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);

		samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		samplerDesc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
		m_pRootSignature->AddStaticSampler(2, samplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);

		m_pRootSignature->Finalize(m_pDevice.Get(), rootSignatureFlags);

		//Pipeline state
		m_pPipelineStateObject = std::make_unique<GraphicsPipelineState>();
		m_pPipelineStateObject->SetInputLayout(inputElements, sizeof(inputElements) / sizeof(inputElements[0]));
		m_pPipelineStateObject->SetRootSignature(m_pRootSignature->GetRootSignature());
		m_pPipelineStateObject->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
		m_pPipelineStateObject->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
		m_pPipelineStateObject->SetRenderTargetFormat(RENDER_TARGET_FORMAT, DEPTH_STENCIL_FORMAT, m_SampleCount, m_SampleQuality);
		m_pPipelineStateObject->SetDepthTest(D3D12_COMPARISON_FUNC_LESS_EQUAL);
		m_pPipelineStateObject->Finalize(m_pDevice.Get());

		//Debug version
		pixelShader.Load("Resources/Diffuse.hlsl", Shader::Type::PixelShader, "PSMain", { "DEBUG_VISUALIZE" });

		m_pPipelineStateObjectDebug = std::make_unique<GraphicsPipelineState>();
		m_pPipelineStateObjectDebug->SetInputLayout(inputElements, sizeof(inputElements) / sizeof(inputElements[0]));
		m_pPipelineStateObjectDebug->SetRootSignature(m_pRootSignature->GetRootSignature());
		m_pPipelineStateObjectDebug->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
		m_pPipelineStateObjectDebug->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
		m_pPipelineStateObjectDebug->SetRenderTargetFormat(RENDER_TARGET_FORMAT, DEPTH_STENCIL_FORMAT, m_SampleCount, m_SampleQuality);
		m_pPipelineStateObjectDebug->SetDepthTest(D3D12_COMPARISON_FUNC_LESS_EQUAL);
		m_pPipelineStateObjectDebug->Finalize(m_pDevice.Get());
	}

	{
		Shader vertexShader;
		vertexShader.Load("Resources/Shadows.hlsl", Shader::Type::VertexShader, "VSMain");

		//Rootsignature
		m_pShadowsRootSignature = std::make_unique<RootSignature>(1);
		m_pShadowsRootSignature->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);

		D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

		m_pShadowsRootSignature->Finalize(m_pDevice.Get(), rootSignatureFlags);

		//Pipeline state
		m_pShadowsPipelineStateObject = std::make_unique<GraphicsPipelineState>();
		m_pShadowsPipelineStateObject->SetInputLayout(inputElements, sizeof(inputElements) / sizeof(inputElements[0]));
		m_pShadowsPipelineStateObject->SetRootSignature(m_pShadowsRootSignature->GetRootSignature());
		m_pShadowsPipelineStateObject->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
		m_pShadowsPipelineStateObject->SetRenderTargetFormats(nullptr, 0, DXGI_FORMAT_D16_UNORM, 1, 0);
		m_pShadowsPipelineStateObject->SetCullMode(D3D12_CULL_MODE_NONE);
		m_pShadowsPipelineStateObject->SetDepthBias(0, 0.0f, 4.0f);
		m_pShadowsPipelineStateObject->Finalize(m_pDevice.Get());

		m_pShadowMap = std::make_unique<Texture2D>();
		m_pShadowMap->Create(this, 2048, 2048, DXGI_FORMAT_D16_UNORM, TextureUsage::DepthStencil | TextureUsage::ShaderResource, 1);
	}

	{
		Shader vertexShader;
		vertexShader.Load("Resources/Shadows.hlsl", Shader::Type::VertexShader, "VSMain");

		//Rootsignature
		m_pDepthPrepassRootSignature = std::make_unique<RootSignature>(1);
		m_pDepthPrepassRootSignature->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);

		D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

		m_pDepthPrepassRootSignature->Finalize(m_pDevice.Get(), rootSignatureFlags);

		//Pipeline state
		m_pDepthPrepassPipelineStateObject = std::make_unique<GraphicsPipelineState>();
		m_pDepthPrepassPipelineStateObject->SetInputLayout(inputElements, sizeof(inputElements) / sizeof(inputElements[0]));
		m_pDepthPrepassPipelineStateObject->SetRootSignature(m_pDepthPrepassRootSignature->GetRootSignature());
		m_pDepthPrepassPipelineStateObject->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
		m_pDepthPrepassPipelineStateObject->SetRenderTargetFormats(nullptr, 0, DEPTH_STENCIL_FORMAT, m_SampleCount, m_SampleQuality);
		m_pDepthPrepassPipelineStateObject->Finalize(m_pDevice.Get());
	}

	{
		Shader computeShader;
		computeShader.Load("Resources/LightCulling.hlsl", Shader::Type::ComputeShader, "CSMain");

		m_pComputeLightCullRootSignature = std::make_unique<RootSignature>(3);
		m_pComputeLightCullRootSignature->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
		m_pComputeLightCullRootSignature->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 3, D3D12_SHADER_VISIBILITY_ALL);
		m_pComputeLightCullRootSignature->SetDescriptorTableSimple(2, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, D3D12_SHADER_VISIBILITY_ALL);
		m_pComputeLightCullRootSignature->Finalize(m_pDevice.Get(), D3D12_ROOT_SIGNATURE_FLAG_NONE);

		m_pComputeLightCullPipeline = std::make_unique<ComputePipelineState>();
		m_pComputeLightCullPipeline->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pComputeLightCullPipeline->SetRootSignature(m_pComputeLightCullRootSignature->GetRootSignature());
		m_pComputeLightCullPipeline->Finalize(m_pDevice.Get());

		m_pLightIndexCounterBuffer = std::make_unique<StructuredBuffer>();
		m_pLightIndexCounterBuffer->Create(this, sizeof(uint32), 1, false);
		m_pLightIndexListBuffer = std::make_unique<StructuredBuffer>();
		m_pLightIndexListBuffer->Create(this, sizeof(uint32), 720000, false);

		m_pLightBuffer = std::make_unique<StructuredBuffer>();
		m_pLightBuffer->Create(this, sizeof(Light), 2048, false);
	}

	//Geometry
	m_pMesh = std::make_unique<Mesh>();
	m_pMesh->Load("Resources/sponza/sponza.dae", this, pContext);

	pContext->Execute(true);

}

void Graphics::UpdateImGui()
{
	for (int i = 1; i < m_FrameTimes.size(); ++i)
	{
		m_FrameTimes[i - 1] = m_FrameTimes[i];
	}
	m_FrameTimes[m_FrameTimes.size() - 1] = GameTimer::DeltaTime();

	ImGui::SetNextWindowPos(ImVec2(0, 0), 0, ImVec2(0, 0));
	ImGui::SetNextWindowSize(ImVec2(250, (float)m_WindowHeight));
	ImGui::Begin("GPU Stats", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
	ImGui::Text("MS: %.4f", GameTimer::DeltaTime() * 1000.0f);
	ImGui::SameLine(100);
	ImGui::Text("FPS: %.1f", 1.0f / GameTimer::DeltaTime());
	ImGui::PlotLines("Frametime", m_FrameTimes.data(), (int)m_FrameTimes.size(), 0, 0, 0.0f, 0.03f, ImVec2(200, 100));
	ImGui::BeginTabBar("GpuStatsBar");
	if (ImGui::BeginTabItem("Descriptor Heaps"))
	{
		ImGui::Text("Used CPU Descriptor Heaps");
		for (const auto& pAllocator : m_DescriptorHeaps)
		{
			switch (pAllocator->GetType())
			{
			case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
				ImGui::TextWrapped("Constant/Shader/Unordered Access Views");
				break;
			case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
				ImGui::TextWrapped("Samplers");
				break;
			case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:
				ImGui::TextWrapped("Render Target Views");
				break;
			case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:
				ImGui::TextWrapped("Depth Stencil Views");
				break;
			default:
				break;
			}
			uint32 totalDescriptors = pAllocator->GetHeapCount() * DescriptorAllocator::DESCRIPTORS_PER_HEAP;
			uint32 usedDescriptors = pAllocator->GetNumAllocatedDescriptors();
			std::stringstream str;
			str << usedDescriptors << "/" << totalDescriptors;
			ImGui::ProgressBar((float)usedDescriptors / totalDescriptors, ImVec2(-1, 0), str.str().c_str());
		}
		ImGui::EndTabItem();
	}
	if (ImGui::BeginTabItem("Memory"))
	{
		ImGui::Text("Used Dynamic Memory: %d KB", m_pDynamicCpuVisibleAllocator->GetTotalMemoryAllocated() / 1024);
		ImGui::Text("Dynamic Memory Peak: %d KB", m_pDynamicCpuVisibleAllocator->GetTotalMemoryAllocatedPeak() / 1024);
		ImGui::EndTabItem();
	}
	ImGui::EndTabBar();
	ImGui::End();
}

CommandQueue* Graphics::GetCommandQueue(D3D12_COMMAND_LIST_TYPE type) const
{
	return m_CommandQueues.at(type).get();
}

CommandContext* Graphics::AllocateCommandContext(D3D12_COMMAND_LIST_TYPE type)
{
	int typeIndex = (int)type;

	std::lock_guard<std::mutex> lockGuard(m_ContextAllocationMutex);
	if (m_FreeCommandLists[typeIndex].size() > 0)
	{
		CommandContext* pCommandList = m_FreeCommandLists[typeIndex].front();
		m_FreeCommandLists[typeIndex].pop();
		pCommandList->Reset();
		return pCommandList;
	}
	else
	{
		ComPtr<ID3D12CommandList> pCommandList;
		ID3D12CommandAllocator* pAllocator = m_CommandQueues[type]->RequestAllocator();
		m_pDevice->CreateCommandList(0, type, pAllocator, nullptr, IID_PPV_ARGS(pCommandList.GetAddressOf()));
		m_CommandLists.push_back(std::move(pCommandList));
		switch (type)
		{
		case D3D12_COMMAND_LIST_TYPE_DIRECT:
			m_CommandListPool[typeIndex].emplace_back(std::make_unique<GraphicsCommandContext>(this, static_cast<ID3D12GraphicsCommandList*>(m_CommandLists.back().Get()), pAllocator));
			break;
		case D3D12_COMMAND_LIST_TYPE_COMPUTE:
			m_CommandListPool[typeIndex].emplace_back(std::make_unique<ComputeCommandContext>(this, static_cast<ID3D12GraphicsCommandList*>(m_CommandLists.back().Get()), pAllocator));
			break;
		default:
			assert(false);
			break;
		}
		return m_CommandListPool[typeIndex].back().get();
	}
}

bool Graphics::IsFenceComplete(uint64 fenceValue)
{
	D3D12_COMMAND_LIST_TYPE type = (D3D12_COMMAND_LIST_TYPE)(fenceValue >> 56);
	CommandQueue* pQueue = GetCommandQueue(type);
	return pQueue->IsFenceComplete(fenceValue);
}

void Graphics::WaitForFence(uint64 fenceValue)
{
	D3D12_COMMAND_LIST_TYPE type = (D3D12_COMMAND_LIST_TYPE)(fenceValue >> 56);
	CommandQueue* pQueue = GetCommandQueue(type);
	pQueue->WaitForFence(fenceValue);
}

void Graphics::FreeCommandList(CommandContext* pCommandList)
{
	std::lock_guard<std::mutex> lockGuard(m_ContextAllocationMutex);
	m_FreeCommandLists[(int)pCommandList->GetType()].push(pCommandList);
}

D3D12_CPU_DESCRIPTOR_HANDLE Graphics::AllocateCpuDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE type)
{
	return m_DescriptorHeaps[type]->AllocateDescriptor();
}

void Graphics::IdleGPU()
{
	for (auto& pCommandQueue : m_CommandQueues)
	{
		if (pCommandQueue)
		{
			pCommandQueue->WaitForIdle();
		}
	}
}

uint32 Graphics::GetMultiSampleQualityLevel(uint32 msaa)
{
	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS qualityLevels;
	qualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	qualityLevels.Format = RENDER_TARGET_FORMAT;
	qualityLevels.NumQualityLevels = 0;
	qualityLevels.SampleCount = msaa;
	HR(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &qualityLevels, sizeof(qualityLevels)));
	return qualityLevels.NumQualityLevels - 1;
}