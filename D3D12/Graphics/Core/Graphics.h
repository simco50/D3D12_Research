#pragma once
#include "../Light.h"
#include "Core/BitField.h"
#include "DescriptorHandle.h"

class CommandQueue;
class CommandContext;
class OfflineDescriptorAllocator;
class ImGuiRenderer;
class DynamicAllocationManager;
class GraphicsResource;
class RootSignature;
class Texture;
class PipelineState;
class Mesh;
class Buffer;
class SubMesh;
struct Material;
class ClusteredForward;
class TiledForward;
class Camera;
class UnorderedAccessView;
class RTAO;
class RTReflections;
class SSAO;
class GpuParticles;
class ShaderManager;
class PipelineStateInitializer;
class StateObject;
class StateObjectInitializer;
class GlobalOnlineDescriptorHeap;
class ResourceView;

#if PLATFORM_WINDOWS
using WindowHandle = HWND;
using WindowHandlePtr = HWND;
#elif PLATFORM_UWP
#include "agile.h"
using WindowHandle = Windows::UI::Core::CoreWindow^;
using WindowHandlePtr = Platform::Agile<Windows::UI::Core::CoreWindow>;
#endif

enum class DefaultTexture
{
	White2D,
	Black2D,
	Magenta2D,
	Gray2D,
	Normal2D,
	BlackCube,
	MAX,
};

struct MaterialData
{
	int Diffuse;
	int Normal;
	int Roughness;
	int Metallic;
};

struct Batch
{
	enum class Blending
	{
		Opaque = 1,
		AlphaMask = 2,
		AlphaBlend = 4,
	};
	int Index = 0;
	Blending BlendMode = Blending::Opaque;
	const SubMesh* pMesh = nullptr;
	MaterialData Material;
	Matrix WorldMatrix;
	BoundingBox Bounds;
};
DECLARE_BITMASK_TYPE(Batch::Blending)

constexpr const int MAX_SHADOW_CASTERS = 32;
struct ShadowData
{
	Matrix LightViewProjections[MAX_SHADOW_CASTERS];
	float CascadeDepths[4];
	uint32 NumCascades = 0;
	uint32 ShadowMapOffset = 0;
};

struct SceneData
{
	Texture* pResolvedDepth = nullptr;
	Texture* pDepthBuffer = nullptr;
	Texture* pRenderTarget = nullptr;
	Texture* pResolvedTarget = nullptr;
	Texture* pPreviousColor = nullptr;
	Texture* pNormals = nullptr;
	Texture* pResolvedNormals = nullptr;
	Texture* pAO = nullptr;
	std::vector<Batch> Batches;
	DescriptorHandle GlobalSRVHeapHandle{};
	Buffer* pLightBuffer = nullptr;
	Camera* pCamera = nullptr;
	ShadowData* pShadowData = nullptr;
	Buffer* pTLAS = nullptr;
	int FrameIndex = 0;
	BitField<128> VisibilityMask;
};

enum class RenderPath
{
	Tiled,
	Clustered,
};

class Graphics
{
public:
	Graphics(uint32 width, uint32 height, int sampleCount = 1);
	~Graphics();

	void Initialize(WindowHandle window);
	void Update();
	void Shutdown();
	void OnResize(int width, int height);

	bool IsFenceComplete(uint64 fenceValue);
	void WaitForFence(uint64 fenceValue);
	void IdleGPU();

	IDXGISwapChain3* GetSwapchain() const { return m_pSwapchain.Get(); }
	inline ID3D12Device* GetDevice() const { return m_pDevice.Get(); }
	inline ID3D12Device5* GetRaytracingDevice() const { return m_pRaytracingDevice.Get(); }
	ImGuiRenderer* GetImGui() const { return m_pImGuiRenderer.get(); }
	CommandQueue* GetCommandQueue(D3D12_COMMAND_LIST_TYPE type) const;
	CommandContext* AllocateCommandContext(D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT);
	void FreeCommandList(CommandContext* pCommandList);

	uint32 GetWindowWidth() const { return m_WindowWidth; }
	uint32 GetWindowHeight() const { return m_WindowHeight; }

	bool CheckTypedUAVSupport(DXGI_FORMAT format) const;
	bool UseRenderPasses() const;
	bool SupportsRayTracing() const { return m_RayTracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED; }
	bool SupportsMeshShaders() const { return m_MeshShaderSupport != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED; }
	bool GetShaderModel(int& major, int& minor) const;

	ShaderManager* GetShaderManager() const { return m_pShaderManager.get(); }
	DynamicAllocationManager* GetAllocationManager() const { return m_pDynamicAllocationManager.get(); }

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

	GlobalOnlineDescriptorHeap* GetGlobalViewHeap() const { return m_pGlobalViewHeap.get(); }

	Texture* GetDefaultTexture(DefaultTexture type) const { return m_DefaultTextures[(int)type].get(); }
	Texture* GetDepthStencil() const { return m_pDepthStencil.get(); }
	Texture* GetResolvedDepthStencil() const { return m_pResolvedDepthStencil.get(); }
	Texture* GetCurrentRenderTarget() const { return m_SampleCount > 1 ? m_pMultiSampleRenderTarget.get() : m_pHDRRenderTarget.get(); }
	Texture* GetCurrentBackbuffer() const { return m_Backbuffers[m_CurrentBackBufferIndex].get(); }

	uint32 GetMultiSampleCount() const { return m_SampleCount; }

	ID3D12Resource* CreateResource(const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES initialState, D3D12_HEAP_TYPE heapType, D3D12_CLEAR_VALUE* pClearValue = nullptr);
	PipelineState* CreatePipeline(const PipelineStateInitializer& psoDesc);
	StateObject* CreateStateObject(const StateObjectInitializer& stateDesc);

	//CONSTANTS
	static const int32 FRAME_COUNT = 3;
	static const DXGI_FORMAT DEPTH_STENCIL_FORMAT;
	static const DXGI_FORMAT DEPTH_STENCIL_SHADOW_FORMAT;
	static const DXGI_FORMAT RENDER_TARGET_FORMAT;
	static const DXGI_FORMAT SWAPCHAIN_FORMAT;

private:
	void BeginFrame();
	void EndFrame(uint64 fenceValue);

	void InitD3D();
	void InitializePipelines();
	void InitializeAssets(CommandContext& context);

	void UpdateImGui();

	ComPtr<ID3D12Device> m_pDevice;
	ComPtr<ID3D12Device5> m_pRaytracingDevice;
	ComPtr<ID3D12Fence> m_pDeviceRemovalFence;
	HANDLE m_DeviceRemovedEvent = 0;

	D3D12_RENDER_PASS_TIER m_RenderPassTier = D3D12_RENDER_PASS_TIER_0;
	D3D12_RAYTRACING_TIER m_RayTracingTier = D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
	uint8 m_ShaderModelMajor = 0;
	uint8 m_ShaderModelMinor = 0;
	D3D12_MESH_SHADER_TIER m_MeshShaderSupport = D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;
	D3D12_SAMPLER_FEEDBACK_TIER m_SamplerFeedbackSupport = D3D12_SAMPLER_FEEDBACK_TIER_NOT_SUPPORTED;
	D3D12_VARIABLE_SHADING_RATE_TIER m_VRSTier = D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED;
	int m_VRSTileSize = -1;

	std::unique_ptr<class OnlineDescriptorAllocator> m_pPersistentDescriptorHeap;
	std::unique_ptr<GlobalOnlineDescriptorHeap> m_pGlobalViewHeap;

	std::array<std::unique_ptr<OfflineDescriptorAllocator>, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES> m_DescriptorHeaps;
	std::unique_ptr<DynamicAllocationManager> m_pDynamicAllocationManager;

	WindowHandlePtr m_pWindow{};
	unsigned int m_WindowWidth;
	unsigned int m_WindowHeight;
	uint32 m_CurrentBackBufferIndex = 0;
	ComPtr<IDXGISwapChain3> m_pSwapchain;
	std::array<std::unique_ptr<Texture>, FRAME_COUNT> m_Backbuffers;

	std::unique_ptr<ShaderManager> m_pShaderManager;

	std::vector<std::unique_ptr<PipelineState>> m_Pipelines;
	std::vector<std::unique_ptr<StateObject>> m_StateObjects;

	int m_Frame = 0;
	std::array<float, 180> m_FrameTimes{};

	std::array<std::unique_ptr<CommandQueue>, D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE> m_CommandQueues;
	std::array<std::vector<std::unique_ptr<CommandContext>>, D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE> m_CommandListPool;
	std::array < std::queue<CommandContext*>, D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE> m_FreeCommandLists;
	std::vector<ComPtr<ID3D12CommandList>> m_CommandLists;
	std::mutex m_ContextAllocationMutex;

	std::unique_ptr<Texture> m_pMultiSampleRenderTarget;
	std::unique_ptr<Texture> m_pHDRRenderTarget;
	std::unique_ptr<Texture> m_pPreviousColor;
	std::unique_ptr<Texture> m_pTonemapTarget;
	std::unique_ptr<Texture> m_pDepthStencil;
	std::unique_ptr<Texture> m_pResolvedDepthStencil;
	std::unique_ptr<Texture> m_pTAASource;
	std::unique_ptr<Texture> m_pVelocity;
	std::unique_ptr<Texture> m_pNormals;
	std::unique_ptr<Texture> m_pResolvedNormals;
	std::vector<std::unique_ptr<Texture>> m_ShadowMaps;

	std::unique_ptr<ImGuiRenderer> m_pImGuiRenderer;
	std::unique_ptr<ClusteredForward> m_pClusteredForward;
	std::unique_ptr<TiledForward> m_pTiledForward;
	std::unique_ptr<RTAO> m_pRTAO;
	std::unique_ptr<RTReflections> m_pRTReflections;
	std::unique_ptr<SSAO> m_pSSAO;

	std::array<std::unique_ptr<Texture>, (int)DefaultTexture::MAX> m_DefaultTextures;

	int m_SampleCount = 1;
	std::unique_ptr<Camera> m_pCamera;

	std::unique_ptr<Buffer> m_pScreenshotBuffer;
	int32 m_ScreenshotDelay = -1;
	uint32 m_ScreenshotRowPitch = 0;

	RenderPath m_RenderPath = RenderPath::Clustered;

	std::vector<std::unique_ptr<Mesh>> m_Meshes;
	std::unique_ptr<Buffer> m_pTLAS;
	std::unique_ptr<Buffer> m_pTLASScratch;

	//Shadow mapping
	std::unique_ptr<RootSignature> m_pShadowsRS;
	PipelineState* m_pShadowsOpaquePSO = nullptr;
	PipelineState* m_pShadowsAlphaMaskPSO = nullptr;
	
	//Depth Prepass
	std::unique_ptr<RootSignature> m_pDepthPrepassRS;
	PipelineState* m_pDepthPrepassOpaquePSO = nullptr;
	PipelineState* m_pDepthPrepassAlphaMaskPSO = nullptr;

	//MSAA Depth resolve
	std::unique_ptr<RootSignature> m_pResolveDepthRS;
	PipelineState* m_pResolveDepthPSO = nullptr;

	//Tonemapping
	std::unique_ptr<Texture> m_pDownscaledColor;
	std::unique_ptr<RootSignature> m_pLuminanceHistogramRS;
	PipelineState* m_pLuminanceHistogramPSO = nullptr;
	std::unique_ptr<RootSignature> m_pAverageLuminanceRS;
	PipelineState* m_pAverageLuminancePSO = nullptr;
	std::unique_ptr<RootSignature> m_pToneMapRS;
	PipelineState* m_pToneMapPSO = nullptr;
	PipelineState* m_pDrawHistogramPSO = nullptr;
	std::unique_ptr<RootSignature> m_pDrawHistogramRS;
	std::unique_ptr<Buffer> m_pLuminanceHistogram;
	std::unique_ptr<Buffer> m_pAverageLuminance;

	//SSAO
	std::unique_ptr<Texture> m_pAmbientOcclusion;

	//Mip generation
	PipelineState* m_pGenerateMipsPSO = nullptr;
	std::unique_ptr<RootSignature> m_pGenerateMipsRS;

	//Depth Reduction
	PipelineState* m_pPrepareReduceDepthPSO = nullptr;
	PipelineState* m_pPrepareReduceDepthMsaaPSO = nullptr;
	PipelineState* m_pReduceDepthPSO = nullptr;
	std::unique_ptr<RootSignature> m_pReduceDepthRS;
	std::vector<std::unique_ptr<Texture>> m_ReductionTargets;
	std::vector<std::unique_ptr<Buffer>> m_ReductionReadbackTargets;

	//Camera motion
	PipelineState* m_pCameraMotionPSO = nullptr;
	std::unique_ptr<RootSignature> m_pCameraMotionRS;

	//TAA
	PipelineState* m_pTemporalResolvePSO = nullptr;
	std::unique_ptr<RootSignature> m_pTemporalResolveRS;

	//Sky
	std::unique_ptr<RootSignature> m_pSkyboxRS;
	PipelineState* m_pSkyboxPSO = nullptr;

	//Particles
	std::unique_ptr<GpuParticles> m_pParticles;

	//Light data
	std::vector<Light> m_Lights;
	std::unique_ptr<Buffer> m_pLightBuffer;

	Texture* m_pVisualizeTexture = nullptr;
	SceneData m_SceneData;
	bool m_CapturePix = false;

	std::map<ResourceView*, int> m_ViewToDescriptorIndex;
	int RegisterBindlessResource(ResourceView* pView, ResourceView* pFallback = nullptr);
	int RegisterBindlessResource(Texture* pTexture, Texture* pFallback = nullptr);
};
