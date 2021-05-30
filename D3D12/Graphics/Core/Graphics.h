#pragma once

#include "Shader.h"
#include "DescriptorHandle.h"

class CommandQueue;
class CommandContext;
class OfflineDescriptorAllocator;
class DynamicAllocationManager;
class GraphicsResource;
class RootSignature;
class Texture;
class PipelineState;
class Buffer;
class ShaderManager;
class PipelineStateInitializer;
class StateObject;
class StateObjectInitializer;
class GlobalOnlineDescriptorHeap;
class ResourceView;
class SwapChain;
class GraphicsDevice;
class Fence;

#if PLATFORM_WINDOWS
using WindowHandle = HWND;
using WindowHandlePtr = HWND;
#elif PLATFORM_UWP
#include "winrt/Windows.UI.Core.h"
using WindowHandle = const winrt::Windows::UI::Core::CoreWindow*;
using WindowHandlePtr = const winrt::Windows::UI::Core::CoreWindow*;
#endif

enum class GraphicsInstanceFlags
{
	None			= 0,
	DebugDevice		= 1 << 0,
	DRED			= 1 << 1,
	GpuValidation	= 1 << 2,
	Pix				= 1 << 3,
};
DECLARE_BITMASK_TYPE(GraphicsInstanceFlags);

class GraphicsInstance
{
public:
	GraphicsInstance(GraphicsInstanceFlags createFlags);
	std::unique_ptr<SwapChain> CreateSwapchain(GraphicsDevice* pDevice, WindowHandle pNativeWindow, DXGI_FORMAT format, uint32 width, uint32 height, uint32 numFrames, bool vsync);
	ComPtr<IDXGIAdapter4> EnumerateAdapter(bool useWarp);
	std::unique_ptr<GraphicsDevice> CreateDevice(ComPtr<IDXGIAdapter4> pAdapter);

	static std::unique_ptr<GraphicsInstance> CreateInstance(GraphicsInstanceFlags createFlags = GraphicsInstanceFlags::None);

	bool AllowTearing() const { return m_AllowTearing; }

private:
	ComPtr<IDXGIFactory6> m_pFactory;
	bool m_AllowTearing = false;
};

class SwapChain
{
public:
	SwapChain(GraphicsDevice* pDevice, IDXGIFactory6* pFactory, WindowHandle pNativeWindow, DXGI_FORMAT format, uint32 width, uint32 height, uint32 numFrames, bool vsync);
	void Destroy();
	void OnResize(uint32 width, uint32 height);
	void Present();

	void SetVsync(bool enabled) { m_Vsync = enabled; }
	IDXGISwapChain4* GetSwapChain() const { return m_pSwapchain.Get(); }
	Texture* GetBackBuffer() const { return m_Backbuffers[m_CurrentImage].get(); }
	Texture* GetBackBuffer(uint32 index) const { return m_Backbuffers[index].get(); }
	uint32 GetBackbufferIndex() const { return m_CurrentImage; }
	DXGI_FORMAT GetFormat() const { return m_Format; }

private:
	std::vector<std::unique_ptr<Texture>> m_Backbuffers;
	ComPtr<IDXGISwapChain4> m_pSwapchain;
	DXGI_FORMAT m_Format;
	uint32 m_CurrentImage;
	bool m_Vsync;
};

class GraphicsDevice
{
public:
	static const DXGI_FORMAT DEPTH_STENCIL_FORMAT;
	static const DXGI_FORMAT RENDER_TARGET_FORMAT;

	GraphicsDevice(IDXGIAdapter4* pAdapter);
	~GraphicsDevice();
	void Destroy();
	void GarbageCollect();

	bool IsFenceComplete(uint64 fenceValue);
	void WaitForFence(uint64 fenceValue);
	void IdleGPU();

	int RegisterBindlessResource(ResourceView* pView, ResourceView* pFallback = nullptr);
	int RegisterBindlessResource(Texture* pTexture, Texture* pFallback = nullptr);

	CommandQueue* GetCommandQueue(D3D12_COMMAND_LIST_TYPE type) const;
	CommandContext* AllocateCommandContext(D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT);
	void FreeCommandList(CommandContext* pCommandList);

	DescriptorHandle GetViewHeapHandle() const;
	GlobalOnlineDescriptorHeap* GetGlobalViewHeap() const { return m_pGlobalViewHeap.get(); }

	template<typename DESC_TYPE>
	struct DescriptorSelector {};
	template<> struct DescriptorSelector<D3D12_SHADER_RESOURCE_VIEW_DESC> { static constexpr D3D12_DESCRIPTOR_HEAP_TYPE Type() { return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; } };
	template<> struct DescriptorSelector<D3D12_UNORDERED_ACCESS_VIEW_DESC> { static constexpr D3D12_DESCRIPTOR_HEAP_TYPE Type() { return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; } };
	template<> struct DescriptorSelector<D3D12_CONSTANT_BUFFER_VIEW_DESC> { static constexpr D3D12_DESCRIPTOR_HEAP_TYPE Type() { return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; } };
	template<> struct DescriptorSelector<D3D12_RENDER_TARGET_VIEW_DESC> { static constexpr D3D12_DESCRIPTOR_HEAP_TYPE Type() { return D3D12_DESCRIPTOR_HEAP_TYPE_RTV; } };
	template<> struct DescriptorSelector<D3D12_DEPTH_STENCIL_VIEW_DESC> { static constexpr D3D12_DESCRIPTOR_HEAP_TYPE Type() { return D3D12_DESCRIPTOR_HEAP_TYPE_DSV; } };

	template<typename DESC_TYPE>
	D3D12_CPU_DESCRIPTOR_HANDLE AllocateDescriptor()
	{
		return m_DescriptorHeaps[DescriptorSelector<DESC_TYPE>::Type()]->AllocateDescriptor();
	}

	template<typename DESC_TYPE>
	void FreeDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
	{
		m_DescriptorHeaps[DescriptorSelector<DESC_TYPE>::Type()]->FreeDescriptor(descriptor);
	}

	bool SupportsTypedUAV(DXGI_FORMAT format) const;
	bool SupportsRenderPasses() const;
	bool SupportsRayTracing() const { return m_RayTracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED; }
	bool SupportsMeshShaders() const { return m_MeshShaderSupport != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED; }

	ID3D12Resource* CreateResource(const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES initialState, D3D12_HEAP_TYPE heapType, D3D12_CLEAR_VALUE* pClearValue = nullptr);
	PipelineState* CreatePipeline(const PipelineStateInitializer& psoDesc);
	StateObject* CreateStateObject(const StateObjectInitializer& stateDesc);

	Shader* GetShader(const char* pShaderPath, ShaderType shaderType, const char* entryPoint = "", const std::vector<ShaderDefine>& defines = {});
	ShaderLibrary* GetLibrary(const char* pShaderPath, const std::vector<ShaderDefine>& defines = {});

	ID3D12Device* GetDevice() const { return m_pDevice.Get(); }
	ID3D12Device5* GetRaytracingDevice() const { return m_pRaytracingDevice.Get(); }
	ShaderManager* GetShaderManager() const { return m_pShaderManager.get(); }

private:
	ComPtr<ID3D12Device> m_pDevice;
	ComPtr<ID3D12Device5> m_pRaytracingDevice;

	HANDLE m_DeviceRemovedEvent = 0;
	std::unique_ptr<Fence> m_pDeviceRemovalFence;

	std::unique_ptr<ShaderManager> m_pShaderManager;

	std::unique_ptr<class OnlineDescriptorAllocator> m_pPersistentDescriptorHeap;
	std::unique_ptr<GlobalOnlineDescriptorHeap> m_pGlobalViewHeap;

	std::array<std::unique_ptr<OfflineDescriptorAllocator>, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES> m_DescriptorHeaps;
	std::unique_ptr<DynamicAllocationManager> m_pDynamicAllocationManager;

	std::vector<std::unique_ptr<PipelineState>> m_Pipelines;
	std::vector<std::unique_ptr<StateObject>> m_StateObjects;

	std::array<std::unique_ptr<CommandQueue>, D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE> m_CommandQueues;
	std::array<std::vector<std::unique_ptr<CommandContext>>, D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE> m_CommandListPool;
	std::array < std::queue<CommandContext*>, D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE> m_FreeCommandLists;
	std::vector<ComPtr<ID3D12CommandList>> m_CommandLists;
	std::mutex m_ContextAllocationMutex;

	D3D12_RENDER_PASS_TIER m_RenderPassTier = D3D12_RENDER_PASS_TIER_0;
	D3D12_RAYTRACING_TIER m_RayTracingTier = D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
	uint8 m_ShaderModelMajor = 0;
	uint8 m_ShaderModelMinor = 0;
	D3D12_MESH_SHADER_TIER m_MeshShaderSupport = D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;
	D3D12_SAMPLER_FEEDBACK_TIER m_SamplerFeedbackSupport = D3D12_SAMPLER_FEEDBACK_TIER_NOT_SUPPORTED;
	D3D12_VARIABLE_SHADING_RATE_TIER m_VRSTier = D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED;
	int m_VRSTileSize = -1;

	std::map<ResourceView*, int> m_ViewToDescriptorIndex;
};
