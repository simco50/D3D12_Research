#pragma once

#ifdef PLATFORM_UWP
using WindowHandle = Windows::UI::Core::CoreWindow^;
#else
using WindowHandle = HWND;
#endif

class CommandQueue;
class CommandContext;
class DescriptorAllocator;
class DynamicResourceAllocator;
class ImGuiRenderer;
class GraphicsBuffer;
class GraphicsResource;
class RootSignature;
class Texture2D;
class PipelineState;

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
	void IdleGPU();

	CommandQueue* GetCommandQueue(D3D12_COMMAND_LIST_TYPE type) const;
	CommandContext* AllocateCommandContext(D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT);
	void FreeCommandList(CommandContext* pCommandList);

	uint32 GetWindowWidth() const { return m_WindowWidth; }
	uint32 GetWindowHeight() const { return m_WindowHeight; }

	DynamicResourceAllocator* GetCpuVisibleAllocator() const { return m_pDynamicCpuVisibleAllocator.get(); }
private:
	void InitD3D(WindowHandle pWindow);
	void InitializeAssets();
	void CreatePipeline();
	void LoadGeometry();
	void LoadTexture();

	static const uint32 FRAME_COUNT = 2;

	ComPtr<IDXGIFactory3> m_pFactory;
	ComPtr<IDXGISwapChain3> m_pSwapchain;
	ComPtr<ID3D12Device> m_pDevice;
	std::array<std::unique_ptr<GraphicsResource>, FRAME_COUNT> m_RenderTargets;
	std::unique_ptr<GraphicsResource> m_pDepthStencilBuffer;
	std::array<std::unique_ptr<DescriptorAllocator>, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES> m_DescriptorHeaps;
	std::unique_ptr<DescriptorAllocator> m_pTextureGpuDesciptorHeap;
	std::unique_ptr<DynamicResourceAllocator> m_pDynamicCpuVisibleAllocator;
	std::array<std::unique_ptr<CommandQueue>, 1> m_CommandQueues;
	std::vector<std::unique_ptr<CommandContext>> m_CommandListPool;
	std::queue<CommandContext*> m_FreeCommandLists;
	std::vector<ComPtr<ID3D12CommandList>> m_CommandLists;

	std::unique_ptr<ImGuiRenderer> m_pImGuiRenderer;

	FloatRect m_Viewport;
	FloatRect m_ScissorRect;
	unsigned int m_WindowWidth;
	unsigned int m_WindowHeight;

	// Synchronization objects.
	uint32 m_CurrentBackBufferIndex = 0;
	std::array<uint64, FRAME_COUNT> m_FenceValues = {};

	DXGI_FORMAT m_DepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	DXGI_FORMAT m_RenderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

	std::array<D3D12_CPU_DESCRIPTOR_HANDLE, FRAME_COUNT> m_RenderTargetHandles;
	D3D12_CPU_DESCRIPTOR_HANDLE m_DepthStencilHandle;

	std::unique_ptr<Texture2D> m_pTexture;
	D3D12_CPU_DESCRIPTOR_HANDLE m_TextureHandle;
	std::unique_ptr<GraphicsBuffer> m_pVertexBuffer;
	std::unique_ptr <GraphicsBuffer> m_pIndexBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_VertexBufferView;
	D3D12_INDEX_BUFFER_VIEW m_IndexBufferView;
	std::unique_ptr<RootSignature> m_pRootSignature;
	std::unique_ptr<PipelineState> m_pPipelineStateObject;
	int m_IndexCount = 0;
};

