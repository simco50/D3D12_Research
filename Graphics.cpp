#include "stdafx.h"
#include "Graphics.h"


Graphics::Graphics(UINT width, UINT height, std::wstring name):
	m_WindowWidth(width), m_WindowHeight(height)
{
}

void Graphics::OnInit()
{
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

	MakeWindow();
	LoadPipeline();
	LoadAssets();

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
			OnUpdate();
		}
	}
	return;
}

void Graphics::OnUpdate()
{
	OnRender();
}

void Graphics::OnRender()
{
	PopulateCommandList();

	ID3D12CommandList* ppCommandLists[] = { m_pCommandList.Get() };
	m_pCommandQueue->ExecuteCommandLists(1, ppCommandLists);

	m_pSwapChain->Present(1, 0);

	WaitForPreviousFrame();
}

void Graphics::OnDestroy()
{
	// Wait for the GPU to be done with all resources.
	WaitForPreviousFrame();

	CloseHandle(m_FenceEvent);
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

	RECT windowRect = { 0, 0, m_WindowWidth, m_WindowHeight };
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

void Graphics::LoadPipeline()
{
#ifdef _DEBUG
	//Enable debug
	ComPtr<ID3D12Debug> pDebugController;
	HRESULT hr = D3D12GetDebugInterface(IID_PPV_ARGS(&pDebugController));
	if (hr == S_OK)
	{
		pDebugController->EnableDebugLayer();
	}
#endif

	//Create the factory
	ComPtr<IDXGIFactory4> pFactory;
	CreateDXGIFactory1(IID_PPV_ARGS(&pFactory));
	
	UINT adapter = 0;
	IDXGIAdapter1* pAdapter;
	while (pFactory->EnumAdapters1(0, &pAdapter))
	{
		++adapter;
	}
	hr = D3D12CreateDevice(pAdapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_pDevice));
	if (hr != S_OK)
		return;

	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	hr = m_pDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_pCommandQueue));
	if (hr != S_OK)
		return;

	DXGI_SWAP_CHAIN_DESC swapchainDesc = {};
	swapchainDesc.BufferCount = FrameCount;
	swapchainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapchainDesc.BufferDesc.Width = m_WindowWidth;
	swapchainDesc.BufferDesc.Height = m_WindowHeight;
	swapchainDesc.BufferDesc.RefreshRate.Denominator = 60;
	swapchainDesc.BufferDesc.RefreshRate.Numerator = 1;
	swapchainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	swapchainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapchainDesc.Flags = 0;
	swapchainDesc.OutputWindow = m_Hwnd;
	swapchainDesc.SampleDesc.Count = 1;
	swapchainDesc.SampleDesc.Quality = 0;
	swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapchainDesc.Windowed = true;

	IDXGISwapChain* pSwapchain;

	hr = pFactory->CreateSwapChain(m_pCommandQueue.Get(), &swapchainDesc, &pSwapchain);
	if (hr != S_OK)
		return;

	pSwapchain->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&m_pSwapChain);
	pSwapchain->Release();

	pFactory->MakeWindowAssociation(m_Hwnd, DXGI_MWA_NO_ALT_ENTER);

	m_FrameIndex = m_pSwapChain->GetCurrentBackBufferIndex();


	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NumDescriptors = FrameCount;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	m_rtvDescriptorSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	hr = m_pDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_pRtvHeap));
	if (hr != S_OK)
		return;


	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_pRtvHeap->GetCPUDescriptorHandleForHeapStart());

	for (unsigned int i = 0; i < FrameCount; ++i)
	{
		m_pSwapChain->GetBuffer(i, IID_PPV_ARGS(&m_pRenderTargets[i]));
		m_pDevice->CreateRenderTargetView(m_pRenderTargets[i].Get(), nullptr, rtvHandle);
		rtvHandle.Offset(1, m_rtvDescriptorSize);
	}

	hr = m_pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_pCommandAllocator));
	if (hr != S_OK)
		return;
}

void Graphics::LoadAssets()
{
	CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
	rootSignatureDesc.Init(00, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> signature;
	ComPtr<ID3DBlob> error;
	D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
	HRESULT hr = m_pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_pRootSignature));

	if (hr != S_OK)
		return;

	ComPtr<ID3DBlob> vertexShader;
	ComPtr<ID3DBlob> pixelShader;

#if defined(_DEBUG)
	// Enable better shader debugging with the graphics debugging tools.
	UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
	UINT compileFlags = 0;
#endif

	ComPtr<ID3DBlob> errorMsg;

	hr = D3DCompileFromFile(L"shaders.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, &errorMsg);
	if (hr != S_OK)
	{
		string errorMessage((char*)errorMsg->GetBufferPointer(), (char*)errorMsg->GetBufferPointer() + errorMsg->GetBufferSize());
		return;
	}
	errorMsg.Reset();

	hr = D3DCompileFromFile(L"shaders.hlsl", nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, &errorMsg);
	if (hr != S_OK)
	{
		string errorMessage((char*)errorMsg->GetBufferPointer(), (char*)errorMsg->GetBufferPointer() + errorMsg->GetBufferSize());
		return;
	}

	D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psDesc = {};
	psDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
	psDesc.pRootSignature = m_pRootSignature.Get();
	psDesc.VS = { reinterpret_cast<UINT8*>(vertexShader->GetBufferPointer()), vertexShader->GetBufferSize() };
	psDesc.PS = { reinterpret_cast<UINT8*>(pixelShader->GetBufferPointer()), pixelShader->GetBufferSize() };
	psDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psDesc.DepthStencilState.DepthEnable = false;
	psDesc.DepthStencilState.StencilEnable = false;
	psDesc.SampleMask = UINT_MAX;
	psDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psDesc.NumRenderTargets = 1;
	psDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psDesc.SampleDesc.Count = 1;
	hr = m_pDevice->CreateGraphicsPipelineState(&psDesc, IID_PPV_ARGS(&m_pPipelineState));
	if (hr != S_OK)
		return;

	hr = m_pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_pCommandAllocator.Get(), m_pPipelineState.Get(), IID_PPV_ARGS(&m_pCommandList));
	if (hr != S_OK)
		return;

	m_pCommandList->Close();

	vector<Vertex> vertices =
	{
		{ { 0.0f, 0.5f, 0.0f },{ 1.0f, 0.0f, 0.0f, 1.0f } },
		{ { 0.5f, -0.25f, 0.0f },{ 0.0f, 1.0f, 0.0f, 1.0f } },
		{ { -0.5f, -0.25f, 0.0f },{ 0.0f, 0.0f, 1.0f, 1.0f } }
	};

	hr = m_pDevice->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(vertices.size() * sizeof(Vertex)),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&m_pVertexBuffer));
	if (hr != S_OK)
		return;

	UINT8* pVertexDataBegin;
	CD3DX12_RANGE readRange(0, 0);
	hr = m_pVertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin));
	if (hr != S_OK)
		return;
	memcpy(pVertexDataBegin, vertices.data(), vertices.size() * sizeof(Vertex));
	m_pVertexBuffer->Unmap(0, nullptr);

	m_pVertexBufferView.BufferLocation = m_pVertexBuffer->GetGPUVirtualAddress();
	m_pVertexBufferView.StrideInBytes = sizeof(Vertex);
	m_pVertexBufferView.SizeInBytes = vertices.size() * sizeof(Vertex);


	m_pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_pFence));
	m_FenceValue = 1;

	m_FenceEvent = CreateEvent(nullptr, false, false, nullptr);
	if (m_FenceEvent == nullptr)
		return;

	WaitForPreviousFrame();
}

void Graphics::PopulateCommandList()
{
	m_pCommandAllocator->Reset();

	m_pCommandList->Reset(m_pCommandAllocator.Get(), m_pPipelineState.Get());

	m_pCommandList->SetGraphicsRootSignature(m_pRootSignature.Get());
	m_pCommandList->RSSetViewports(1, &m_Viewport);
	m_pCommandList->RSSetScissorRects(1, &m_ScissorRect);

	m_pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_pRenderTargets[m_FrameIndex].Get(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_pRtvHeap->GetCPUDescriptorHandleForHeapStart(), m_FrameIndex, m_rtvDescriptorSize);
	m_pCommandList->OMSetRenderTargets(1, &rtvHandle, false, nullptr);

	const float clearColor[] = { 0.4f, 0.4f, 0.4f, 1.0f };
	m_pCommandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
	m_pCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_pCommandList->IASetVertexBuffers(0, 1, &m_pVertexBufferView);
	m_pCommandList->DrawInstanced(3, 1, 0, 0);

	m_pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_pRenderTargets[m_FrameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	m_pCommandList->Close();
}

void Graphics::WaitForPreviousFrame()
{
	// WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
	// This is code implemented as such for simplicity. More advanced samples 
	// illustrate how to use fences for efficient resource usage.

	// Signal and increment the fence value.
	const UINT64 fence = m_FenceValue;
	m_pCommandQueue->Signal(m_pFence.Get(), fence);
	m_FenceValue++;

	// Wait until the previous frame is finished.
	if (m_pFence->GetCompletedValue() < fence)
	{
		m_pFence->SetEventOnCompletion(fence, m_FenceEvent);
		WaitForSingleObject(m_FenceEvent, INFINITE);
	}

	m_FrameIndex = m_pSwapChain->GetCurrentBackBufferIndex();
}

const UINT Graphics::FrameCount;

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
	case WM_CLOSE:
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

	default:
		break;
	}

	return DefWindowProc(hWnd, message, wParam, lParam);
}