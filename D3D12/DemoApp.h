#pragma once
#include "Graphics/RHI/Graphics.h"
#include "Graphics/Light.h"
#include "Graphics/SceneView.h"
#include "Graphics/RHI/CommandQueue.h"

class Mesh;
class ClusteredForward;
class TiledForward;
class Camera;
class RTAO;
class RTReflections;
class SSAO;
class GpuParticles;
class PathTracing;
class CBTTessellation;
class UnorderedAccessView;
class StateObject;
class RGGraph;
class RGResourcePool;
struct SubMesh;
struct Material;

enum class RenderPath
{
	Tiled,
	Clustered,
	PathTracing,
	Visibility,
	MAX
};

class DemoApp
{
public:
	DemoApp(WindowHandle window, const IntVector2& windowRect);
	~DemoApp();

	void Update();
	void OnResizeOrMove(int width, int height);

private:
	void OnResizeViewport(int width, int height);

	void VisualizeTexture(RGGraph& graph, Texture* pTexture);

	void InitializePipelines();
	void SetupScene(CommandContext& context);

	void UpdateImGui();

	void LoadMesh(const std::string& filePath, CommandContext& context, World& world);
	void CreateShadowViews(SceneView& view, World& world);

	RefCountPtr<GraphicsDevice> m_pDevice;
	RefCountPtr<SwapChain> m_pSwapchain;
	std::unique_ptr<RGResourcePool> m_RenderGraphPool;

	uint32 m_Frame = 0;
	std::array<float, 180> m_FrameTimes{};

	RefCountPtr<Texture> m_pColorHistory;
	RefCountPtr<Texture> m_ColorOutput;
	std::vector<RefCountPtr<Texture>> m_ShadowMaps;

	std::unique_ptr<ClusteredForward> m_pClusteredForward;
	std::unique_ptr<TiledForward> m_pTiledForward;
	std::unique_ptr<RTAO> m_pRTAO;
	std::unique_ptr<RTReflections> m_pRTReflections;
	std::unique_ptr<SSAO> m_pSSAO;
	std::unique_ptr<PathTracing> m_pPathTracing;
	std::unique_ptr<CBTTessellation> m_pCBTTessellation;
	std::unique_ptr<GpuParticles> m_pParticles;

	WindowHandle m_Window = nullptr;
	std::unique_ptr<Camera> m_pCamera;

	struct ScreenshotRequest
	{
		SyncPoint SyncPoint;
		uint32 Width;
		uint32 Height;
		uint32 RowPitch;
		RefCountPtr<Buffer> pBuffer;
	};
	std::queue<ScreenshotRequest> m_ScreenshotBuffers;
	RenderPath m_RenderPath = RenderPath::Clustered;

	World m_World;
	SceneView m_SceneData;

	RefCountPtr<RootSignature> m_pCommonRS;

	//Shadow mapping
	RefCountPtr<PipelineState> m_pShadowsOpaquePSO;
	RefCountPtr<PipelineState> m_pShadowsAlphaMaskPSO;

	//Depth Prepass
	RefCountPtr<PipelineState> m_pDepthPrepassOpaquePSO;
	RefCountPtr<PipelineState> m_pDepthPrepassAlphaMaskPSO;

	//MSAA Depth resolve
	RefCountPtr<PipelineState> m_pResolveDepthPSO;

	//Tonemapping
	RefCountPtr<PipelineState> m_pToneMapPSO;

	// Eye adaptation
	RefCountPtr<Buffer> m_pLuminanceHistogram;
	RefCountPtr<Buffer> m_pAverageLuminance;
	RefCountPtr<Texture> m_pDebugHistogramTexture;
	RefCountPtr<PipelineState> m_pLuminanceHistogramPSO;
	RefCountPtr<PipelineState> m_pAverageLuminancePSO;
	RefCountPtr<PipelineState> m_pDrawHistogramPSO;

	//Mip generation
	RefCountPtr<PipelineState> m_pGenerateMipsPSO;

	//Depth Reduction
	RefCountPtr<PipelineState> m_pPrepareReduceDepthPSO;
	RefCountPtr<PipelineState> m_pPrepareReduceDepthMsaaPSO;
	RefCountPtr<PipelineState> m_pReduceDepthPSO;
	std::vector<RefCountPtr<Buffer>> m_ReductionReadbackTargets;

	//Camera motion
	RefCountPtr<PipelineState> m_pCameraMotionPSO;

	RefCountPtr<PipelineState> m_pTemporalResolvePSO;

	//Sky
	RefCountPtr<PipelineState> m_pSkyboxPSO;
	RefCountPtr<PipelineState> m_pRenderSkyPSO;
	RefCountPtr<Texture> m_pSkyTexture;

	//Bloom
	RefCountPtr<PipelineState> m_pBloomSeparatePSO;
	RefCountPtr<PipelineState> m_pBloomMipChainPSO;
	RefCountPtr<Texture> m_pBloomTexture;
	RefCountPtr<Texture> m_pBloomIntermediateTexture;
	std::vector<RefCountPtr<UnorderedAccessView>> m_pBloomUAVs;
	std::vector<RefCountPtr<UnorderedAccessView>> m_pBloomIntermediateUAVs;

	// Visibility buffer
	RefCountPtr<PipelineState> m_pVisibilityRenderingPSO;
	RefCountPtr<PipelineState> m_pVisibilityRenderingMaskedPSO;
	RefCountPtr<PipelineState> m_pVisibilityShadingPSO;

	// DDGI
	RefCountPtr<StateObject> m_pDDGITraceRaysSO;
	RefCountPtr<PipelineState> m_pDDGIUpdateIrradianceColorPSO;
	RefCountPtr<PipelineState> m_pDDGIUpdateIrradianceDepthPSO;
	RefCountPtr<PipelineState> m_pDDGIUpdateProbeStatesPSO;
	RefCountPtr<PipelineState> m_pDDGIVisualizePSO;
	
	// Debug Visualize
	RefCountPtr<PipelineState> m_pVisualizeTexturePSO;
	struct TextureVisualizeData
	{
		RefCountPtr<Texture> pTarget;
		float RangeMin = 0.0f;
		float RangeMax = 1.0f;
		bool VisibleChannels[4] = { true, true, true, true };
		float MipLevel = 0.0f;
		float Slice = 0.0f;
	} m_VisualizeTextureData;

	bool m_CapturePix = false;
};
