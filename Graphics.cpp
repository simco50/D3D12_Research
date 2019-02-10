#include "stdafx.h"
#include "Graphics.h"
#include "GpuResource.h"
#include "LinearAllocator.h"
#include "Timer.h"
#include <map>

#pragma comment(lib, "dxguid.lib")

const UINT Graphics::FRAME_COUNT;

Graphics::Graphics(UINT width, UINT height, std::wstring name):
	m_WindowWidth(width), m_WindowHeight(height)
{
}

void Graphics::Initialize()
{
	MakeWindow();
	InitD3D();
	OnResize();

	InitializeAssets();

	//Game loop
	MSG msg = {};
	while (msg.message != WM_QUIT)
	{
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
			Update();
			Render();
		}
	}
	return;
}

void Graphics::Update()
{
	Timer(L"Update");
	m_CommandAllocators[m_CurrentBackBufferIndex]->Reset();
	m_pCommandList->Reset(m_CommandAllocators[m_CurrentBackBufferIndex].Get(), m_pPipelineStateObject.Get());

	m_pCommandList->SetGraphicsRootSignature(m_pRootSignature.Get());

	ID3D12DescriptorHeap* ppHeaps[] = { m_pCbvSrvHeap.Get() };
	m_pCommandList->SetDescriptorHeaps(1, ppHeaps);
	m_pCommandList->SetGraphicsRootDescriptorTable(0, m_pCbvSrvHeap->GetGPUDescriptorHandleForHeapStart());

	m_pCommandList->RSSetViewports(1, &m_Viewport);
	m_pCommandList->RSSetScissorRects(1, &m_ScissorRect);

	m_pCommandList->ResourceBarrier(1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			m_RenderTargets[m_CurrentBackBufferIndex].Get(),
			D3D12_RESOURCE_STATE_PRESENT,
			D3D12_RESOURCE_STATE_RENDER_TARGET));

	m_pCommandList->OMSetRenderTargets(1, &GetCurrentBackBufferView(), true, &GetDepthStencilView());

	const float clearColor[] = { 0.4f, 0.4f, 0.4f, 1.0f };
	m_pCommandList->ClearRenderTargetView(GetCurrentBackBufferView(), clearColor, 0, nullptr);
	m_pCommandList->ClearDepthStencilView(GetDepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
	
	m_pCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_pCommandList->IASetVertexBuffers(0, 1, &m_VertexBufferView);
	m_pCommandList->IASetIndexBuffer(&m_IndexBufferView);
	m_pCommandList->DrawIndexedInstanced(6, 1, 0, 0, 0);

	m_pCommandList->ResourceBarrier(1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			m_RenderTargets[m_CurrentBackBufferIndex].Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PRESENT));

	m_pCommandList->Close();
}

void Graphics::CreateRtvAndDsvHeaps()
{
	//Create rtv heap
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.NumDescriptors = FRAME_COUNT;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NodeMask = 0;
	HR(m_pDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(m_pRtvHeap.GetAddressOf())));

	//create dsv heap
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
	dsvHeapDesc.NumDescriptors = 1;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvHeapDesc.NodeMask = 0;
	HR(m_pDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(m_pDsvHeap.GetAddressOf())));
}

void Graphics::Render()
{
	ID3D12CommandList* ppCommandLists[] = { m_pCommandList.Get() };
	m_pCommandQueue->ExecuteCommandLists(1, ppCommandLists);

	m_pSwapchain->Present(1, 0);

	MoveToNextFrame();
}

void Graphics::Shutdown()
{
	// Wait for the GPU to be done with all resources.
	WaitForGPU();
}

ID3D12Resource* Graphics::CurrentBackBuffer() const
{
	return m_RenderTargets[m_CurrentBackBufferIndex].Get();
}

D3D12_CPU_DESCRIPTOR_HANDLE Graphics::GetCurrentBackBufferView() const
{
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(
		m_pRtvHeap->GetCPUDescriptorHandleForHeapStart(), 
		m_CurrentBackBufferIndex, 
		m_RtvDescriptorSize);
}

D3D12_CPU_DESCRIPTOR_HANDLE Graphics::GetDepthStencilView() const
{
	return m_pDsvHeap->GetCPUDescriptorHandleForHeapStart();
}

void Graphics::MakeWindow()
{
	WNDCLASSW wc;

	wc.hInstance = GetModuleHandle(0);
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hIcon = 0;
	wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
	wc.lpfnWndProc = WndProcStatic;
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpszClassName = L"wndClass";
	wc.lpszMenuName = nullptr;
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

	if (!RegisterClass(&wc))
	{
		auto error = GetLastError();
		return;
	}

	int displayWidth = GetSystemMetrics(SM_CXSCREEN);
	int displayHeight = GetSystemMetrics(SM_CYSCREEN);

	DWORD windowStyle = WS_OVERLAPPEDWINDOW;

	RECT windowRect = { 0, 0, (LONG)m_WindowWidth, (LONG)m_WindowHeight };
	AdjustWindowRect(&windowRect, windowStyle, false);
	unsigned int windowWidth = windowRect.right - windowRect.left;
	unsigned int windowHeight = windowRect.bottom - windowRect.top;

	int x = (displayWidth - windowWidth) / 2;
	int y = (displayHeight - windowHeight) / 2;

	m_Hwnd = CreateWindow(
		L"wndClass",
		L"Hello World",
		windowStyle,
		x,
		y,
		windowWidth,
		windowHeight,
		nullptr,
		nullptr,
		GetModuleHandle(0),
		this
	);

	if (m_Hwnd == nullptr)
		return;

	ShowWindow(m_Hwnd, SW_SHOWDEFAULT);
	if (!UpdateWindow(m_Hwnd))
		return;
}

void Graphics::InitD3D()
{
#ifdef _DEBUG
	//Enable debug
	ComPtr<ID3D12Debug> pDebugController;
	HR(D3D12GetDebugInterface(IID_PPV_ARGS(&pDebugController)));
	pDebugController->EnableDebugLayer();
#endif

	//Create the factory
	HR(CreateDXGIFactory1(IID_PPV_ARGS(&m_pFactory)));

	//Create the device
	HR(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_pDevice)));

	//Create the fence
	HR(m_pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_pFence)));

	//Cache the decriptor sizes
	m_RtvDescriptorSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	m_DsvDescriptorSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	m_CbvSrvDescriptorSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	//Check 4x MSAA support
	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS qualityLevels;
	qualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	qualityLevels.Format = m_RenderTargetFormat;
	qualityLevels.NumQualityLevels = 0;
	qualityLevels.SampleCount = 4;
	HR(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &qualityLevels, sizeof(qualityLevels)));

	m_MsaaQuality = qualityLevels.NumQualityLevels;
	if (m_MsaaQuality <= 0)
		return;

	CreateCommandObjects();
	CreateSwapchain();
	CreateRtvAndDsvHeaps();
}

void Graphics::CreateCommandObjects()
{
	//Create command queue
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	HR(m_pDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_pCommandQueue)));

	//Create the command allocator
	for (int i = 0; i < m_CommandAllocators.size(); ++i)
	{
		HR(m_pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(m_CommandAllocators[i].GetAddressOf())));
	}

	//Create the command list
	m_pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_CommandAllocators[m_CurrentBackBufferIndex].Get(), nullptr, IID_PPV_ARGS(m_pCommandList.GetAddressOf()));
	HR(m_pCommandList->Close());
}

void Graphics::CreateSwapchain()
{
	m_pSwapchain.Reset();

	DXGI_SWAP_CHAIN_DESC swapchainDesc = {};
	swapchainDesc.BufferDesc.Width = m_WindowWidth;
	swapchainDesc.BufferDesc.Height = m_WindowHeight;
	swapchainDesc.BufferDesc.RefreshRate.Denominator = 60;
	swapchainDesc.BufferDesc.RefreshRate.Numerator = 1;
	swapchainDesc.BufferDesc.Format = m_RenderTargetFormat;
	swapchainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	swapchainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	swapchainDesc.SampleDesc.Count = 1;
	swapchainDesc.SampleDesc.Quality = 0;
	swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapchainDesc.BufferCount = FRAME_COUNT;
	swapchainDesc.OutputWindow = m_Hwnd;
	swapchainDesc.Windowed = true;
	swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapchainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	HR(m_pFactory->CreateSwapChain(
		m_pCommandQueue.Get(),
		&swapchainDesc,
		&m_pSwapchain));
}

void Graphics::WaitForGPU()
{
	// Schedule a Signal command in the queue.
	HR(m_pCommandQueue->Signal(m_pFence.Get(), m_FenceValues[m_CurrentBackBufferIndex]));

	// Wait until the fence has been processed.
	HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
	HR(m_pFence->SetEventOnCompletion(m_FenceValues[m_CurrentBackBufferIndex], eventHandle));
	WaitForSingleObjectEx(eventHandle, INFINITE, FALSE);
	CloseHandle(eventHandle);

	// Increment the fence value for the current frame.
	m_FenceValues[m_CurrentBackBufferIndex]++;
}

void Graphics::MoveToNextFrame()
{
	const UINT64 currentFenceValue = m_FenceValues[m_CurrentBackBufferIndex];

	m_pCommandQueue->Signal(m_pFence.Get(), currentFenceValue);

	m_CurrentBackBufferIndex = (m_CurrentBackBufferIndex + 1) % 2;

	if (m_pFence->GetCompletedValue() < m_FenceValues[m_CurrentBackBufferIndex])
	{
		HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);

		HR(m_pFence->SetEventOnCompletion(m_FenceValues[m_CurrentBackBufferIndex], eventHandle));
		{
			Timer a(L"Wait for next frame");
			WaitForSingleObject(eventHandle, INFINITE);
		}
		CloseHandle(eventHandle);
	}

	m_FenceValues[m_CurrentBackBufferIndex] = currentFenceValue + 1;
}

void Graphics::OnResize()
{
	WaitForGPU();
	m_pCommandList->Reset(m_CommandAllocators[m_CurrentBackBufferIndex].Get(), nullptr);

	for (int i = 0; i < FRAME_COUNT; ++i)
		m_RenderTargets[i].Reset();
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
	CD3DX12_CPU_DESCRIPTOR_HANDLE handle(m_pRtvHeap->GetCPUDescriptorHandleForHeapStart());
	for (int i = 0; i < FRAME_COUNT; ++i)
	{
		HR(m_pSwapchain->GetBuffer(i, IID_PPV_ARGS(&m_RenderTargets[i])));
		m_pDevice->CreateRenderTargetView(m_RenderTargets[i].Get(), nullptr, handle);
		handle.Offset(1, m_RtvDescriptorSize);
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
	HR(m_pDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&desc,
		D3D12_RESOURCE_STATE_COMMON,
		&clearValue,
		IID_PPV_ARGS(&m_pDepthStencilBuffer)));
	m_pDevice->CreateDepthStencilView(m_pDepthStencilBuffer.Get(), nullptr, GetDepthStencilView());

	m_pCommandList->ResourceBarrier(
		1, 
		&CD3DX12_RESOURCE_BARRIER::Transition(
			m_pDepthStencilBuffer.Get(),
			D3D12_RESOURCE_STATE_COMMON, 
			D3D12_RESOURCE_STATE_DEPTH_WRITE)
	);

	m_pCommandList->Close();
	ID3D12CommandList* pCommandList[] = { m_pCommandList.Get() };
	m_pCommandQueue->ExecuteCommandLists(1, pCommandList);

	WaitForGPU();

	m_Viewport.Height = (float)m_WindowHeight;
	m_Viewport.Width = (float)m_WindowWidth;
	m_Viewport.MaxDepth = 1.0f;
	m_Viewport.MinDepth = 0.0f;
	m_Viewport.TopLeftX = 0.0f;
	m_Viewport.TopLeftY = 0.0f;
	
	m_ScissorRect.left = 0;
	m_ScissorRect.top = 0;
	m_ScissorRect.right = m_WindowWidth;
	m_ScissorRect.bottom = m_WindowHeight;
}

LRESULT CALLBACK Graphics::WndProcStatic(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	Graphics* pThis = nullptr;

	if (message == WM_NCCREATE)
	{
		pThis = static_cast<Graphics*>(reinterpret_cast<CREATESTRUCT*>(lParam)->lpCreateParams);
		SetLastError(0);
		if (!SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis)))
		{
			if (GetLastError() != 0)
				return 0;
		}
	}
	else
	{
		pThis = reinterpret_cast<Graphics*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
	}
	if (pThis)
	{
		LRESULT callback = pThis->WndProc(hWnd, message, wParam, lParam);
		return callback;
	}
	return DefWindowProc(hWnd, message, wParam, lParam);
}

LRESULT Graphics::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
		// WM_SIZE is sent when the user resizes the window.  
	case WM_SIZE:
		// Save the new client area dimensions.
		m_WindowWidth = LOWORD(lParam);
		m_WindowHeight = HIWORD(lParam);
		if (m_pDevice)
		{
			if (wParam == SIZE_MINIMIZED)
			{
				mMinimized = true;
				mMaximized = false;
			}
			else if (wParam == SIZE_MAXIMIZED)
			{
				mMinimized = false;
				mMaximized = true;
				OnResize();
			}
			else if (wParam == SIZE_RESTORED)
			{

				// Restoring from minimized state?
				if (mMinimized)
				{
					mMinimized = false;
					OnResize();
				}

				// Restoring from maximized state?
				else if (mMaximized)
				{
					mMaximized = false;
					OnResize();
				}
				else if (mResizing)
				{
					// If user is dragging the resize bars, we do not resize 
					// the buffers here because as the user continuously 
					// drags the resize bars, a stream of WM_SIZE messages are
					// sent to the window, and it would be pointless (and slow)
					// to resize for each WM_SIZE message received from dragging
					// the resize bars.  So instead, we reset after the user is 
					// done resizing the window and releases the resize bars, which 
					// sends a WM_EXITSIZEMOVE message.
				}
				else // API call such as SetWindowPos or mSwapChain->SetFullscreenState.
				{
					OnResize();
				}
			}
			return 0;
		}
	case WM_KEYUP:
		if (wParam == VK_ESCAPE)
		{
			PostQuitMessage(0);
		}
		return 0;

	// WM_DESTROY is sent when the window is being destroyed.
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}


	return DefWindowProc(hWnd, message, wParam, lParam);
}

void Graphics::InitializeAssets()
{
	m_pCommandList->Reset(m_CommandAllocators[m_CurrentBackBufferIndex].Get(), nullptr);
	BuildDescriptorHeaps();
	BuildConstantBuffers();
	BuildRootSignature();
	BuildShadersAndInputLayout();
	BuildGeometry();
	BuildPSO();
	m_pCommandList->Close();

	ID3D12CommandList* ppCommandLists[] = { m_pCommandList.Get() };
	m_pCommandQueue->ExecuteCommandLists(1, ppCommandLists);

	WaitForGPU();
}

void Graphics::BuildDescriptorHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	heapDesc.NumDescriptors = 2;
	heapDesc.NodeMask = 0;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;

	HR(m_pDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(m_pCbvSrvHeap.GetAddressOf())));
}

void Graphics::BuildConstantBuffers()
{
	// Create the constant buffer.
	{
		struct ConstantBufferData
		{
			XMFLOAT4 Color;
		} Data;
		Data.Color = XMFLOAT4(0.0f, 1.0f, 1.0f, 1.0f);

		int size = (sizeof(ConstantBufferData) + 255) & ~255;

		m_pDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(size),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_pConstantBuffer));

		// Describe and create a constant buffer view.
		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
		cbvDesc.BufferLocation = m_pConstantBuffer->GetGPUVirtualAddress();
		cbvDesc.SizeInBytes = size;    // CB size is required to be 256-byte aligned.
		m_pDevice->CreateConstantBufferView(&cbvDesc, m_pCbvSrvHeap->GetCPUDescriptorHandleForHeapStart());

		// Map and initialize the constant buffer. We don't unmap this until the
		// app closes. Keeping things mapped for the lifetime of the resource is okay.
		CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
		void* pData = nullptr;
		m_pConstantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pData));
		memcpy(pData, &Data, sizeof(Data));
	}
}

void Graphics::BuildRootSignature()
{
	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC desc = {};

	CD3DX12_ROOT_PARAMETER1 rootParameters[1];
	CD3DX12_DESCRIPTOR_RANGE1 range[1];
	range[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
	rootParameters[0].InitAsDescriptorTable(1, range, D3D12_SHADER_VISIBILITY_ALL);

	D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

	desc.Init_1_1(1, rootParameters, 0, nullptr, rootSignatureFlags);
	
	ComPtr<ID3DBlob> pDataBlob, pErrorBlob;
	HR(D3D12SerializeVersionedRootSignature(&desc, pDataBlob.GetAddressOf(), pErrorBlob.GetAddressOf()));
	HR(m_pDevice->CreateRootSignature(0, pDataBlob->GetBufferPointer(), pDataBlob->GetBufferSize(), IID_PPV_ARGS(m_pRootSignature.GetAddressOf())));
}

void Graphics::BuildShadersAndInputLayout()
{
#if defined(_DEBUG)
	// Enable better shader debugging with the graphics debugging tools.
	UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
	UINT compileFlags = 0;
#endif

	ComPtr<ID3DBlob> pErrorBlob;
	D3DCompileFromFile(L"shaders.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, m_pVertexShaderCode.GetAddressOf(), pErrorBlob.GetAddressOf());
	if (pErrorBlob != nullptr)
	{
		wstring errorMsg = wstring((char*)pErrorBlob->GetBufferPointer(), (char*)pErrorBlob->GetBufferPointer() + pErrorBlob->GetBufferSize());
		wcout << errorMsg << endl;
		return;
	}
	pErrorBlob.Reset();
	D3DCompileFromFile(L"shaders.hlsl", nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, m_pPixelShaderCode.GetAddressOf(), pErrorBlob.GetAddressOf());
	if (pErrorBlob != nullptr)
	{
		wstring errorMsg = wstring((char*)pErrorBlob->GetBufferPointer(), (char*)pErrorBlob->GetBufferPointer() + pErrorBlob->GetBufferSize());
		wcout << errorMsg << endl;
		return;
	}

	//Input layout
	m_InputElements.push_back(D3D12_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
	m_InputElements.push_back(D3D12_INPUT_ELEMENT_DESC{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });


	//Shader reflection reference
	ComPtr<ID3D12ShaderReflection> pShaderReflection;
	D3D12_SHADER_DESC shaderDesc;
	D3DReflect(m_pPixelShaderCode->GetBufferPointer(), m_pPixelShaderCode->GetBufferSize(), IID_ID3D11ShaderReflection, (void**)pShaderReflection.GetAddressOf());
	pShaderReflection->GetDesc(&shaderDesc);

	std::map<std::string, int> cbRegisterMap;

	for (unsigned i = 0; i < shaderDesc.BoundResources; ++i)
	{
		D3D12_SHADER_INPUT_BIND_DESC resourceDesc;
		pShaderReflection->GetResourceBindingDesc(i, &resourceDesc);

		switch (resourceDesc.Type)
		{
		case D3D_SIT_CBUFFER:
		case D3D_SIT_TBUFFER:
			cbRegisterMap[resourceDesc.Name] = resourceDesc.BindPoint;
			break;
		case D3D_SIT_TEXTURE:
		case D3D_SIT_SAMPLER:
		case D3D_SIT_UAV_RWTYPED:
		case D3D_SIT_STRUCTURED:
		case D3D_SIT_UAV_RWSTRUCTURED:
		case D3D_SIT_BYTEADDRESS:
		case D3D_SIT_UAV_RWBYTEADDRESS:
		case D3D_SIT_UAV_APPEND_STRUCTURED:
		case D3D_SIT_UAV_CONSUME_STRUCTURED:
		case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
		default:
			break;
		}
	}

	for (unsigned int c = 0; c < shaderDesc.ConstantBuffers; ++c)
	{
		ID3D12ShaderReflectionConstantBuffer* pReflectionConstantBuffer = pShaderReflection->GetConstantBufferByIndex(c);
		D3D12_SHADER_BUFFER_DESC bufferDesc;
		pReflectionConstantBuffer->GetDesc(&bufferDesc);
		unsigned int cbRegister = cbRegisterMap[std::string(bufferDesc.Name)];

		//ConstantBuffer* pConstantBuffer = pGraphics->GetOrCreateConstantBuffer(cbRegister, bufferDesc.Size);
		//m_ConstantBuffers[cbRegister] = pConstantBuffer;
		//m_ConstantBufferSizes[cbRegister] = bufferDesc.Size;

		for (unsigned v = 0; v < bufferDesc.Variables; ++v)
		{
			ID3D12ShaderReflectionVariable* pVariable = pReflectionConstantBuffer->GetVariableByIndex(v);
			D3D12_SHADER_VARIABLE_DESC variableDesc;
			pVariable->GetDesc(&variableDesc);
			std::string name = variableDesc.Name;

			//ShaderParameter parameter = {};
			//parameter.Name = variableDesc.Name;
			//parameter.Offset = variableDesc.StartOffset;
			//parameter.Size = variableDesc.Size;
			//parameter.Buffer = cbRegister;
			//parameter.pBuffer = pConstantBuffer;
			//m_ShaderParameters[StringHash(variableDesc.Name)] = parameter;
		}
	}
}

void Graphics::BuildGeometry()
{
	{
		//Vertex buffer
		vector<PosColVertex> vertices =
		{
			PosColVertex({ XMFLOAT3(-1.0f, -1.0f, 0.0f), XMFLOAT4(Colors::White) }),
			PosColVertex({ XMFLOAT3(-1.0f, +1.0f, 0.0f), XMFLOAT4(Colors::Black) }),
			PosColVertex({ XMFLOAT3(+1.0f, +1.0f, 0.0f), XMFLOAT4(Colors::Red) }),
			PosColVertex({ XMFLOAT3(+1.0f, -1.0f, 0.0f), XMFLOAT4(Colors::Red) }),
		};

		m_pDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(sizeof(vertices) * sizeof(PosColVertex)),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(m_pVertexBuffer.GetAddressOf()));

		m_pDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(sizeof(vertices) * sizeof(PosColVertex)),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(pVertexUploadBuffer.GetAddressOf()));

		D3D12_SUBRESOURCE_DATA subResourceData = {};
		subResourceData.pData = vertices.data();
		subResourceData.RowPitch = vertices.size() * sizeof(PosColVertex);
		subResourceData.SlicePitch = subResourceData.RowPitch;

		m_pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_pVertexBuffer.Get(), D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_COPY_DEST));
		UpdateSubresources(m_pCommandList.Get(), m_pVertexBuffer.Get(), pVertexUploadBuffer.Get(), 0, 0, 1, &subResourceData);
		m_pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_pVertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ));

		m_VertexBufferView.BufferLocation = m_pVertexBuffer->GetGPUVirtualAddress();
		m_VertexBufferView.SizeInBytes = sizeof(PosColVertex) * vertices.size();
		m_VertexBufferView.StrideInBytes = sizeof(PosColVertex);

	}

	{
		//Index buffer
		vector<unsigned int> indices =
		{
			0, 1, 2, 0, 2, 3
		};

		m_pDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(sizeof(indices) * sizeof(unsigned int)),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(m_pIndexBuffer.GetAddressOf()));

		m_pDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(sizeof(indices) * sizeof(unsigned int)),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(pIndexUploadBuffer.GetAddressOf()));

		D3D12_SUBRESOURCE_DATA subResourceData = {};
		subResourceData.pData = indices.data();
		subResourceData.RowPitch = indices.size() * sizeof(unsigned int);
		subResourceData.SlicePitch = subResourceData.RowPitch;

		m_pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_pIndexBuffer.Get(), D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_COPY_DEST));
		UpdateSubresources(m_pCommandList.Get(), m_pIndexBuffer.Get(), pIndexUploadBuffer.Get(), 0, 0, 1, &subResourceData);
		m_pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_pIndexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ));

		m_IndexBufferView.BufferLocation = m_pIndexBuffer->GetGPUVirtualAddress();
		m_IndexBufferView.SizeInBytes = sizeof(unsigned int) * indices.size();
		m_IndexBufferView.Format = DXGI_FORMAT_R32_UINT;
	}
}

void Graphics::BuildPSO()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
	desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	desc.DSVFormat = m_DepthStencilFormat;
	desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	desc.InputLayout.NumElements = m_InputElements.size();
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