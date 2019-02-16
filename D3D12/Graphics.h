#pragma once

#ifdef PLATFORM_UWP
using WindowHandle = Windows::UI::Core::CoreWindow^;
#else
using WindowHandle = HWND;
#endif

class CommandQueue;
class CommandContext;

class Graphics
{
public:
	Graphics(uint32 width, uint32 height);
	~Graphics();

	virtual void Initialize(WindowHandle window);
	virtual void Update();
	virtual void Shutdown();

	ID3D12Device* GetDevice() const { return m_pDevice.Get(); }
	void OnResize(int width, int height);

	void WaitForFence(uint64 fenceValue);
	CommandQueue* GetCommandQueue(D3D12_COMMAND_LIST_TYPE type) const;
	CommandContext* AllocateCommandList(D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT);
	void FreeCommandList(CommandContext* pCommandList);

	void IdleGPU();
private:
	static const uint32 FRAME_COUNT = 2;

	std::array<std::unique_ptr<CommandQueue>, 1> m_CommandQueues;
	std::vector<std::unique_ptr<CommandContext>> m_CommandListPool;
	std::queue<CommandContext*> m_FreeCommandLists;
	std::vector<ComPtr<ID3D12CommandList>> m_CommandLists;

	// Pipeline objects.
	Rect m_Viewport;
	Rect m_ScissorRect;
	ComPtr<IDXGIFactory3> m_pFactory;
	ComPtr<IDXGISwapChain3> m_pSwapchain;
	ComPtr<ID3D12Device> m_pDevice;
	std::array<ComPtr<ID3D12Resource>, FRAME_COUNT> m_RenderTargets;
	ComPtr<ID3D12Resource> m_pDepthStencilBuffer;

	D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentBackBufferView() const;
	D3D12_CPU_DESCRIPTOR_HANDLE GetDepthStencilView() const;
	ID3D12Resource* CurrentBackBuffer() const;

	uint32 m_RtvDescriptorSize;
	uint32 m_DsvDescriptorSize;
	uint32 m_CbvSrvDescriptorSize;

	uint32 m_MsaaQuality = 0;

	ComPtr<ID3D12DescriptorHeap> m_pRtvHeap;
	ComPtr<ID3D12DescriptorHeap> m_pDsvHeap;

	// Synchronization objects.
	uint32 m_CurrentBackBufferIndex = 0;
	std::array<uint64, FRAME_COUNT> m_FenceValues = {};

	HWND m_Hwnd;

	unsigned int m_WindowWidth;
	unsigned int m_WindowHeight;

	//void MakeWindow();
	void InitD3D(WindowHandle pWindow);
	void CreateSwapchain(WindowHandle pWindow);
	void CreateDescriptorHeaps();

	DXGI_FORMAT m_DepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	DXGI_FORMAT m_RenderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

	void InitializeAssets();
	void BuildConstantBuffers();
	void BuildRootSignature();
	void BuildShadersAndInputLayout();
	void BuildGeometry();
	void BuildPSO();

	ComPtr<ID3D12DescriptorHeap> m_pCbvSrvHeap;
	ComPtr<ID3D12Resource> m_pVertexBuffer;
	ComPtr<ID3D12Resource> m_pIndexBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_VertexBufferView;
	D3D12_INDEX_BUFFER_VIEW m_IndexBufferView;
	ComPtr<ID3D12RootSignature> m_pRootSignature;
	ComPtr<ID3DBlob> m_pVertexShaderCode;
	ComPtr<ID3DBlob> m_pPixelShaderCode;
	std::vector<D3D12_INPUT_ELEMENT_DESC> m_InputElements;
	ComPtr<ID3D12PipelineState> m_pPipelineStateObject;
	ComPtr<ID3D12Resource> m_pConstantBuffer;
	int m_IndexCount = 0;
};

