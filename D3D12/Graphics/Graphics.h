#pragma once
#include "Light.h"

class CommandQueue;
class CommandContext;
class OfflineDescriptorAllocator;
class ImGuiRenderer;
class DynamicAllocationManager;
class GraphicsResource;
class RootSignature;
class Texture;
class GraphicsPipelineState;
class ComputePipelineState;
class Mesh;
class Buffer;
class SubMesh;
struct Material;
class ClusteredForward;
class TiledForward;
class Camera;
class RGResourceAllocator;
class DebugRenderer;
class UnorderedAccessView;
class RTAO;

struct Batch
{
	const SubMesh* pMesh;
	const Material* pMaterial;
	Matrix WorldMatrix;
	BoundingBox Bounds;
};

enum class RenderPath
{
	Tiled,
	Clustered,
};

#define PIX_CAPTURE_SCOPE() PixScopeCapture<__COUNTER__> pix_scope_##__COUNTER__;

template<size_t IDX>
class PixScopeCapture
{
public:
	PixScopeCapture()
	{
		static bool hit = false;
		if (hit == false)
		{
			hit = true;
			DXGIGetDebugInterface1(0, IID_PPV_ARGS(&pGa));
			if (pGa)
				pGa->BeginCapture();
		}
	}

	~PixScopeCapture()
	{
		if (pGa)
			pGa->EndCapture();
	}
private:
	ComPtr<IDXGraphicsAnalysis> pGa;
};

class Graphics
{
public:
	Graphics(uint32 width, uint32 height, int sampleCount = 1);
	~Graphics();

	virtual void Initialize(HWND window);
	virtual void Update();
	virtual void Shutdown();

	inline ID3D12Device* GetDevice() const { return m_pDevice.Get(); }
	inline ID3D12Device5* GetRaytracingDevice() const { return m_pRaytracingDevice.Get(); }
	void OnResize(int width, int height);

	bool IsFenceComplete(uint64 fenceValue);
	void WaitForFence(uint64 fenceValue);
	void IdleGPU();

	CommandQueue* GetCommandQueue(D3D12_COMMAND_LIST_TYPE type) const;
	CommandContext* AllocateCommandContext(D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT);
	void FreeCommandList(CommandContext* pCommandList);

	uint32 GetWindowWidth() const { return m_WindowWidth; }
	uint32 GetWindowHeight() const { return m_WindowHeight; }

	bool CheckTypedUAVSupport(DXGI_FORMAT format) const;
	bool UseRenderPasses() const;
	bool SupportsRayTracing() const { return m_RayTracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED; }
	bool GetShaderModel(int& major, int& minor) const;

	DynamicAllocationManager* GetAllocationManager() const { return m_pDynamicAllocationManager.get(); }

	OfflineDescriptorAllocator* GetDescriptorManager(D3D12_DESCRIPTOR_HEAP_TYPE type) const { return m_DescriptorHeaps[type].get(); }

	Texture* GetDepthStencil() const { return m_pDepthStencil.get(); }
	Texture* GetResolvedDepthStencil() const { return m_SampleCount > 1 ? m_pResolvedDepthStencil.get() : m_pDepthStencil.get(); }
	Texture* GetCurrentRenderTarget() const { return m_SampleCount > 1 ? m_pMultiSampleRenderTarget.get() : m_pHDRRenderTarget.get(); }
	Texture* GetCurrentBackbuffer() const { return m_Backbuffers[m_CurrentBackBufferIndex].get(); }

	Camera* GetCamera() const { return m_pCamera.get(); }

	uint32 GetMultiSampleCount() const { return m_SampleCount; }
	uint32 GetMultiSampleQualityLevel(uint32 msaa);

	ID3D12Resource* CreateResource(const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES initialState, D3D12_HEAP_TYPE heapType, D3D12_CLEAR_VALUE* pClearValue = nullptr);

	//CONSTANTS
	static const int32 SHADOW_MAP_SIZE = 4096;
	static const int32 FRAME_COUNT = 3;
	static const int32 MAX_LIGHT_DENSITY = 720000;
	static const DXGI_FORMAT DEPTH_STENCIL_FORMAT;
	static const DXGI_FORMAT DEPTH_STENCIL_SHADOW_FORMAT;
	static const DXGI_FORMAT RENDER_TARGET_FORMAT;
	static const DXGI_FORMAT SWAPCHAIN_FORMAT;
	static const int FORWARD_PLUS_BLOCK_SIZE = 16;
	static const int MAX_SHADOW_CASTERS = 8;

private:
	void BeginFrame();
	void EndFrame(uint64 fenceValue);

	void InitD3D();
	void InitializeAssets();

	void UpdateImGui();

	void RandomizeLights(int count);

	int m_Frame = 0;
	std::array<float, 180> m_FrameTimes{};

	int m_DesiredLightCount = 4096;

	std::unique_ptr<Camera> m_pCamera;

	HWND m_pWindow = nullptr;

	ComPtr<IDXGISwapChain3> m_pSwapchain;
	ComPtr<ID3D12Device> m_pDevice;
	ComPtr<ID3D12Device5> m_pRaytracingDevice;

	D3D12_RENDER_PASS_TIER m_RenderPassTier = D3D12_RENDER_PASS_TIER_0;
	D3D12_RAYTRACING_TIER m_RayTracingTier = D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
	int m_ShaderModelMajor = -1;
	int m_ShaderModelMinor = -1;

	int m_SampleCount = 1;
	int m_SampleQuality = 0;

	std::array<std::unique_ptr<OfflineDescriptorAllocator>, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES> m_DescriptorHeaps;
	std::unique_ptr<DynamicAllocationManager> m_pDynamicAllocationManager;

	std::array<std::unique_ptr<CommandQueue>, D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE> m_CommandQueues;
	std::array<std::vector<std::unique_ptr<CommandContext>>, D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE> m_CommandListPool;
	std::array < std::queue<CommandContext*>, D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE> m_FreeCommandLists;
	std::vector<ComPtr<ID3D12CommandList>> m_CommandLists;
	std::mutex m_ContextAllocationMutex;

	std::array<std::unique_ptr<Texture>, FRAME_COUNT> m_Backbuffers;
	std::unique_ptr<Texture> m_pMultiSampleRenderTarget;
	std::unique_ptr<Texture> m_pHDRRenderTarget;
	std::unique_ptr<Texture> m_pDepthStencil;
	std::unique_ptr<Texture> m_pResolvedDepthStencil;
	std::unique_ptr<Texture> m_pMSAANormals;
	std::unique_ptr<Texture> m_pNormals;

	std::unique_ptr<ImGuiRenderer> m_pImGuiRenderer;
	std::unique_ptr<RGResourceAllocator> m_pGraphAllocator;
	std::unique_ptr<ClusteredForward> m_pClusteredForward;
	std::unique_ptr<TiledForward> m_pTiledForward;
	std::unique_ptr<RTAO> m_pRaytracing;
	std::unique_ptr<DebugRenderer> m_pDebugRenderer;

	unsigned int m_WindowWidth;
	unsigned int m_WindowHeight;

	// Synchronization objects.
	uint32 m_CurrentBackBufferIndex = 0;
	std::array<uint64, FRAME_COUNT> m_FenceValues = {};

	RenderPath m_RenderPath = RenderPath::Clustered;

	std::unique_ptr<Mesh> m_pMesh;
	std::vector<Batch> m_OpaqueBatches;
	std::vector<Batch> m_TransparantBatches;

	//Shadow mapping
	std::unique_ptr<Texture> m_pShadowMap;
	std::unique_ptr<RootSignature> m_pShadowsRS;
	std::unique_ptr<GraphicsPipelineState> m_pShadowsOpaquePSO;
	std::unique_ptr<GraphicsPipelineState> m_pShadowsAlphaPSO;
	
	//Depth Prepass
	std::unique_ptr<RootSignature> m_pDepthPrepassRS;
	std::unique_ptr<GraphicsPipelineState> m_pDepthPrepassPSO;

	//MSAA Depth resolve
	std::unique_ptr<RootSignature> m_pResolveDepthRS;
	std::unique_ptr<ComputePipelineState> m_pResolveDepthPSO;

	//Tonemapping
	std::unique_ptr<Texture> m_pDownscaledColor;
	std::unique_ptr<RootSignature> m_pLuminanceHistogramRS;
	std::unique_ptr<ComputePipelineState> m_pLuminanceHistogramPSO;
	std::unique_ptr<RootSignature> m_pAverageLuminanceRS;
	std::unique_ptr<ComputePipelineState> m_pAverageLuminancePSO;
	std::unique_ptr<RootSignature> m_pToneMapRS;
	std::unique_ptr<GraphicsPipelineState> m_pToneMapPSO;
	std::unique_ptr<Buffer> m_pLuminanceHistogram;
	std::unique_ptr<Texture> m_pAverageLuminance;

	//SSAO
	std::unique_ptr<Texture> m_pNoiseTexture;
	std::unique_ptr<Texture> m_pAmbientOcclusion;
	std::unique_ptr<Texture> m_pAmbientOcclusionIntermediate;
	std::unique_ptr<RootSignature> m_pSSAORS;
	std::unique_ptr<ComputePipelineState> m_pSSAOPSO;
	std::unique_ptr<RootSignature> m_pSSAOBlurRS;
	std::unique_ptr<ComputePipelineState> m_pSSAOBlurPSO;

	//Mip generation
	std::unique_ptr<ComputePipelineState> m_pGenerateMipsPSO;
	std::unique_ptr<RootSignature> m_pGenerateMipsRS;

	//Light data
	int m_ShadowCasters = 0;
	std::vector<Light> m_Lights;
	std::unique_ptr<Buffer> m_pLightBuffer;
};