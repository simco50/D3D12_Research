#pragma once
#include "RHI.h"
#include "Shader.h"
#include "DescriptorHandle.h"
#include "DeviceResource.h"
#include "D3D.h"
#include "Fence.h"
#include "ScratchAllocator.h"
#include "Texture.h"
#include "Buffer.h"

class ScratchAllocationManager;
class ShaderManager;
class GPUDescriptorHeap;
class SwapChain;
class CommandSignatureInitializer;
class RingBufferAllocator;

using WindowHandle = HWND;

enum class DisplayMode
{
	SDR,
	HDR_PQ,
	HDR_scRGB,
};

class SwapChain : public DeviceObject
{
public:
	SwapChain(GraphicsDevice* pDevice, DisplayMode displayMode, uint32 numFrames, WindowHandle pNativeWindow);
	~SwapChain();
	void OnResizeOrMove(uint32 width, uint32 height);
	void Present();

	void SetNumFrames(uint32 numFrames);
	uint32 GetNumFrames() const { return m_NumFrames; }
	void SetMaxFrameLatency(uint32 maxFrameLatency);
	uint32 GetMaxFrameLatency() const { return m_MaxFrameLatency; }
	void SetUseWaitableSwapChain(bool enabled);
	bool GetUseWaitableSwapChain() const { return m_UseWaitableObject; }

	void SetDisplayMode(DisplayMode displayMode) { m_DesiredDisplayMode = displayMode; }
	void SetVSync(bool enabled) { m_Vsync = enabled; }
	bool DisplaySupportsHDR() const;

	Vector2i GetViewport() const;
	IDXGISwapChainX* GetSwapChain() const { return m_pSwapchain.Get(); }
	Texture* GetBackBuffer() const { return m_Backbuffers[m_CurrentImage]; }
	Texture* GetBackBuffer(uint32 index) const { return m_Backbuffers[index]; }
	uint32 GetBackbufferIndex() const { return m_CurrentImage; }
	ResourceFormat GetFormat() const { return m_Format; }
	bool GetVSync() const { return m_Vsync; }

private:
	void ReleaseBackbuffers();
	void RecreateSwapChain();

	WindowHandle m_Window;
	DisplayMode m_DesiredDisplayMode;
	Ref<Fence> m_pPresentFence;
	Array<Ref<Texture>> m_Backbuffers;
	Ref<IDXGISwapChainX> m_pSwapchain;
	ResourceFormat m_Format;
	uint32 m_CurrentImage;
	uint32 m_Width = 0;
	uint32 m_Height = 0;
	uint32 m_NumFrames;
	uint32 m_MaxFrameLatency = 2;
	HANDLE m_WaitableObject = nullptr;
	bool m_UseWaitableObject = true;
	bool m_Vsync = true;
	bool m_AllowTearing = false;
};

struct GraphicsDeviceOptions
{
	bool UseDebugDevice = false;
	bool UseDRED = false;
	bool UseGPUValidation = false;
	bool LoadPIX = false;
	bool UseWarp = false;
	bool UseStablePowerState = false;
};

class GraphicsCapabilities
{
public:
	void Initialize(GraphicsDevice* pDevice);

	bool SupportsRaytracing() const { return RayTracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED; }
	bool SupportsMeshShading() const { return MeshShaderSupport != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED; }
	bool SupportsVRS() const { return VRSTier != D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED; }
	bool SupportsSamplerFeedback() const { return SamplerFeedbackSupport != D3D12_SAMPLER_FEEDBACK_TIER_NOT_SUPPORTED; }
	bool SupportsWorkGraphs() const { return m_FeatureSupport.WorkGraphsTier() != D3D12_WORK_GRAPHS_TIER_NOT_SUPPORTED; }
	void GetShaderModel(uint8& maj, uint8& min) const { maj = (uint8)(ShaderModel >> 0x4); min = (uint8)(ShaderModel & 0xF); }
	bool CheckUAVSupport(DXGI_FORMAT format) const;

	D3D12_RENDER_PASS_TIER RenderPassTier = D3D12_RENDER_PASS_TIER_0;
	D3D12_RAYTRACING_TIER RayTracingTier = D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
	uint16 ShaderModel = (D3D_SHADER_MODEL)0;
	D3D12_MESH_SHADER_TIER MeshShaderSupport = D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;
	D3D12_SAMPLER_FEEDBACK_TIER SamplerFeedbackSupport = D3D12_SAMPLER_FEEDBACK_TIER_NOT_SUPPORTED;
	D3D12_VARIABLE_SHADING_RATE_TIER VRSTier = D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED;
	int VRSTileSize = -1;

private:
	GraphicsDevice* m_pDevice = nullptr;
	CD3DX12FeatureSupport m_FeatureSupport;
};

class GraphicsDevice : public DeviceObject
{
public:
	static const uint32 NUM_BUFFERS = 2;

	GraphicsDevice(GraphicsDeviceOptions options);
	~GraphicsDevice();

	void						TickFrame();
	void						IdleGPU();

	CommandQueue*				GetGraphicsQueue() const { return m_GraphicsQueue; }
	CommandQueue*				GetComputeQueue() const { return m_ComputeQueue; }
	CommandQueue*				GetCopyQueue() const { return m_CopyQueue; }

	CommandContext*				AllocateCommandContext(D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT);
	void						FreeCommandList(CommandContext* pCommandList);
	Ref<ID3D12CommandAllocator> AllocateCommandAllocator(D3D12_COMMAND_LIST_TYPE type);
	void						FreeCommandAllocator(Ref<ID3D12CommandAllocator>& pAllocator, D3D12_COMMAND_LIST_TYPE type, const SyncPoint& syncPoint);

	void						ReleaseResourceDescriptor(DescriptorHandle& handle);
	DescriptorPtr				FindResourceDescriptorPtr(DescriptorHandle handle);

	Ref<Texture>				CreateTexture(const TextureDesc& desc, const char* pName, Span<D3D12_SUBRESOURCE_DATA> initData = {});
	Ref<Texture>				CreateTexture(const TextureDesc& desc, ID3D12Heap* pHeap, uint64 offset, const char* pName, Span<D3D12_SUBRESOURCE_DATA> initData = {});
	Ref<Texture>				CreateTextureForSwapchain(ID3D12ResourceX* pSwapchainResource, uint32 index);
	Ref<Buffer>					CreateBuffer(const BufferDesc& desc, ID3D12Heap* pHeap, uint64 offset, const char* pName, const void* pInitData = nullptr);
	Ref<Buffer>					CreateBuffer(const BufferDesc& desc, const char* pName, const void* pInitData = nullptr);
	Ref<PipelineState>			CreatePipeline(const PipelineStateInitializer& psoDesc);
	Ref<PipelineState>			CreateComputePipeline(RootSignature* pRootSignature, const char* pShaderPath, const char* entryPoint = "", Span<ShaderDefine> defines = {});
	Ref<StateObject>			CreateStateObject(const StateObjectInitializer& stateDesc);
	BufferView					CreateSRV(const Buffer* pBuffer, const BufferSRVDesc& desc);
	RWBufferView				CreateUAV(const Buffer* pBuffer, const BufferUAVDesc& desc);
	TextureView					CreateSRV(const Texture* pTexture, const TextureSRVDesc& desc);
	RWTextureView				CreateUAV(const Texture* pTexture, const TextureUAVDesc& desc);
	Ref<CommandSignature>		CreateCommandSignature(const CommandSignatureInitializer& signatureDesc, const char* pName, RootSignature* pRootSignature = nullptr);

	void						DeferReleaseObject(ID3D12Object* pObject);

	ShaderResult				GetShader(const char* pShaderPath, ShaderType shaderType, const char* entryPoint = "", Span<ShaderDefine> defines = {});
	ShaderResult				GetLibrary(const char* pShaderPath, Span<ShaderDefine> defines = {});

	RingBufferAllocator*		GetRingBuffer() const { return m_pRingBufferAllocator; }
	GPUDescriptorHeap*			GetGlobalViewHeap() const { return m_pGlobalViewHeap; }
	GPUDescriptorHeap*			GetGlobalSamplerHeap() const { return m_pGlobalSamplerHeap; }
	ID3D12Device5*				GetDevice() const { return m_pDevice.Get(); }
	ShaderManager*				GetShaderManager() const { return m_pShaderManager.get(); }
	const GraphicsCapabilities& GetCapabilities() const { return m_Capabilities; }
	Fence*						GetFrameFence() const { return m_pFrameFence; }
	IDXGIFactory6*				GetFactory() const { return m_pFactory; }

private:
	struct LiveObjectReporter
	{
		~LiveObjectReporter();
	} Reporter;

	GraphicsCapabilities m_Capabilities;

	Ref<IDXGIFactoryX> m_pFactory;
	Ref<ID3D12DeviceX> m_pDevice;

	class DRED
	{
	public:
		DRED(GraphicsDevice* pDevice);
		~DRED();

		Ref<Fence> m_pFence;
		HANDLE m_WaitHandle;
	};
	std::unique_ptr<DRED> m_pDRED;

	Ref<Fence> m_pFrameFence;
	StaticArray<uint64, NUM_BUFFERS> m_FrameFenceValues{};
	uint32 m_FrameIndex = 0;

	Ref<CommandQueue> m_GraphicsQueue;
	Ref<CommandQueue> m_ComputeQueue;
	Ref<CommandQueue> m_CopyQueue;

	Ref<GPUDescriptorHeap> m_pGlobalViewHeap;
	Ref<GPUDescriptorHeap> m_pGlobalSamplerHeap;

	StaticArray<Array<Ref<CommandContext>>, D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE> m_CommandListPool;
	StaticArray<std::queue<CommandContext*>, D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE> m_FreeCommandLists;
	StaticArray<FencedPool<Ref<ID3D12CommandAllocator>, true>, D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE> m_CommandAllocatorPool;

	class DeferredDeleteQueue : public DeviceObject
	{
	private:
		struct FencedObject
		{
			Fence* pFence;
			uint64 FenceValue;
			ID3D12Object* pResource;
		};

	public:
		DeferredDeleteQueue(GraphicsDevice* pParent);
		~DeferredDeleteQueue();

		void EnqueueResource(ID3D12Object* pResource, Fence* pFence);

		void Clean();
	private:
		std::mutex m_QueueCS;
		std::queue<FencedObject> m_DeletionQueue;
	};

	DeferredDeleteQueue m_DeleteQueue;

	std::unique_ptr<ShaderManager> m_pShaderManager;
	Ref<ScratchAllocationManager> m_pScratchAllocationManager;
	Ref<RingBufferAllocator> m_pRingBufferAllocator;

	std::mutex m_ContextAllocationMutex;
};
