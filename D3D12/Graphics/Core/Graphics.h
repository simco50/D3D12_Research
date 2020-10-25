#pragma once
#include "../Light.h"

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

#if PLATFORM_WINDOWS
using WindowHandle = HWND;
using WindowHandlePtr = HWND;
#elif PLATFORM_UWP
#include "agile.h"
using WindowHandle = Windows::UI::Core::CoreWindow^;
using WindowHandlePtr = Platform::Agile<Windows::UI::Core::CoreWindow>;
#endif

struct MaterialData
{
	int Diffuse;
	int Normal;
	int Roughness;
	int Metallic;
};

struct Batch
{
	const SubMesh* pMesh = nullptr;
	MaterialData Material;
	Matrix WorldMatrix;
	BoundingBox Bounds;
};

constexpr const int MAX_SHADOW_CASTERS = 32;
struct ShadowData
{
	Matrix LightViewProjections[MAX_SHADOW_CASTERS];
	Vector4 ShadowMapOffsets[MAX_SHADOW_CASTERS];
	float CascadeDepths[4];
	uint32 NumCascades = 0;
};

struct SceneData
{
	Texture* pResolvedDepth = nullptr;
	Texture* pDepthBuffer = nullptr;
	Texture* pRenderTarget = nullptr;
	Texture* pPreviousColor = nullptr;
	Texture* pAO = nullptr;
	Mesh* pMesh = nullptr;
	std::vector<std::unique_ptr<Texture>>* pShadowMaps = nullptr;
	std::vector<Batch> OpaqueBatches;
	std::vector<Batch> TransparantBatches;
	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> MaterialTextures;
	Buffer* pLightBuffer = nullptr;
	Camera* pCamera = nullptr;
	ShadowData* pShadowData = nullptr;
	Buffer* pTLAS = nullptr;
	int FrameIndex = 0;
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

	Texture* GetDepthStencil() const { return m_pDepthStencil.get(); }
	Texture* GetResolvedDepthStencil() const { return m_SampleCount > 1 ? m_pResolvedDepthStencil.get() : m_pDepthStencil.get(); }
	Texture* GetCurrentRenderTarget() const { return m_SampleCount > 1 ? m_pMultiSampleRenderTarget.get() : m_pHDRRenderTarget.get(); }
	Texture* GetCurrentBackbuffer() const { return m_Backbuffers[m_CurrentBackBufferIndex].get(); }

	uint32 GetMultiSampleCount() const { return m_SampleCount; }
	uint32 GetMaxMSAAQuality(uint32 msaa, DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN);

	ID3D12Resource* CreateResource(const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES initialState, D3D12_HEAP_TYPE heapType, D3D12_CLEAR_VALUE* pClearValue = nullptr);

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

	ComPtr<IDXGISwapChain3> m_pSwapchain;
	ComPtr<ID3D12Device> m_pDevice;
	ComPtr<ID3D12Device5> m_pRaytracingDevice;

	int m_Frame = 0;
	std::array<float, 180> m_FrameTimes{};

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
	std::unique_ptr<Texture> m_pPreviousColor;
	std::unique_ptr<Texture> m_pTonemapTarget;
	std::unique_ptr<Texture> m_pDepthStencil;
	std::unique_ptr<Texture> m_pResolvedDepthStencil;
	std::vector<std::unique_ptr<Texture>> m_ShadowMaps;

	std::unique_ptr<ImGuiRenderer> m_pImGuiRenderer;
	std::unique_ptr<ClusteredForward> m_pClusteredForward;
	std::unique_ptr<TiledForward> m_pTiledForward;
	std::unique_ptr<RTAO> m_pRTAO;
	std::unique_ptr<RTReflections> m_pRTReflections;
	std::unique_ptr<SSAO> m_pSSAO;

	std::unique_ptr<Camera> m_pCamera;
	WindowHandlePtr m_pWindow{};

	D3D12_RENDER_PASS_TIER m_RenderPassTier = D3D12_RENDER_PASS_TIER_0;
	D3D12_RAYTRACING_TIER m_RayTracingTier = D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
	int m_ShaderModelMajor = -1;
	int m_ShaderModelMinor = -1;
	D3D12_MESH_SHADER_TIER m_MeshShaderSupport = D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;
	D3D12_SAMPLER_FEEDBACK_TIER m_SamplerFeedbackSupport = D3D12_SAMPLER_FEEDBACK_TIER_NOT_SUPPORTED;

	int m_SampleCount = 1;

	unsigned int m_WindowWidth;
	unsigned int m_WindowHeight;
	std::unique_ptr<Buffer> m_pScreenshotBuffer;
	int32 m_ScreenshotDelay = -1;
	uint32 m_ScreenshotRowPitch = 0;

	uint32 m_CurrentBackBufferIndex = 0;

	RenderPath m_RenderPath = RenderPath::Clustered;

	std::unique_ptr<Mesh> m_pMesh;
	std::unique_ptr<Buffer> m_pBLAS;
	std::unique_ptr<Buffer> m_pTLAS;
	std::unique_ptr<Buffer> m_pBLASScratch;
	std::unique_ptr<Buffer> m_pTLASScratch;

	//Shadow mapping
	std::unique_ptr<RootSignature> m_pShadowsRS;
	std::unique_ptr<PipelineState> m_pShadowsOpaquePSO;
	std::unique_ptr<PipelineState> m_pShadowsAlphaPSO;
	
	//Depth Prepass
	std::unique_ptr<RootSignature> m_pDepthPrepassRS;
	std::unique_ptr<PipelineState> m_pDepthPrepassPSO;

	//MSAA Depth resolve
	std::unique_ptr<RootSignature> m_pResolveDepthRS;
	std::unique_ptr<PipelineState> m_pResolveDepthPSO;

	//Tonemapping
	std::unique_ptr<Texture> m_pDownscaledColor;
	std::unique_ptr<RootSignature> m_pLuminanceHistogramRS;
	std::unique_ptr<PipelineState> m_pLuminanceHistogramPSO;
	std::unique_ptr<RootSignature> m_pAverageLuminanceRS;
	std::unique_ptr<PipelineState> m_pAverageLuminancePSO;
	std::unique_ptr<RootSignature> m_pToneMapRS;
	std::unique_ptr<PipelineState> m_pToneMapPSO;
	std::unique_ptr<PipelineState> m_pDrawHistogramPSO;
	std::unique_ptr<RootSignature> m_pDrawHistogramRS;
	std::unique_ptr<Buffer> m_pLuminanceHistogram;
	std::unique_ptr<Buffer> m_pAverageLuminance;

	//SSAO
	std::unique_ptr<Texture> m_pAmbientOcclusion;

	//Mip generation
	std::unique_ptr<PipelineState> m_pGenerateMipsPSO;
	std::unique_ptr<RootSignature> m_pGenerateMipsRS;

	//Depth Reduction
	std::unique_ptr<PipelineState> m_pPrepareReduceDepthPSO;
	std::unique_ptr<PipelineState> m_pPrepareReduceDepthMsaaPSO;
	std::unique_ptr<PipelineState> m_pReduceDepthPSO;
	std::unique_ptr<RootSignature> m_pReduceDepthRS;
	std::vector<std::unique_ptr<Texture>> m_ReductionTargets;
	std::vector<std::unique_ptr<Buffer>> m_ReductionReadbackTargets;

	//Sky
	std::unique_ptr<RootSignature> m_pSkyboxRS;
	std::unique_ptr<PipelineState> m_pSkyboxPSO;

	//Particles
	std::unique_ptr<GpuParticles> m_pParticles;

	//Light data
	std::vector<Light> m_Lights;
	std::unique_ptr<Buffer> m_pLightBuffer;

	Texture* m_pVisualizeTexture = nullptr;
	SceneData m_SceneData;
	bool m_CapturePix = false;
};