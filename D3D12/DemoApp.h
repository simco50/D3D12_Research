#pragma once
#include "Graphics/RHI/RHI.h"
#include "Graphics/RHI/Graphics.h"
#include "Graphics/Light.h"
#include "Graphics/SceneView.h"
#include "Graphics/RHI/CommandQueue.h"
#include "Graphics/Profiler.h"
#include "Graphics/Techniques/ClusteredForward.h"
#include "Graphics/Techniques/TiledForward.h"

class Mesh;
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
class Clouds;
class PipelineState;
class ShaderDebugRenderer;
class GPUDrivenRenderer;
class DDGI;
class VisualizeTexture;
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
	DemoApp(WindowHandle window, const Vector2i& windowRect);
	~DemoApp();

	void Update();
	void OnResizeOrMove(int width, int height);

private:
	void OnResizeViewport(int width, int height);

	void InitializePipelines();
	void SetupScene(CommandContext& context);

	void UpdateImGui();

	void LoadMesh(const std::string& filePath, CommandContext& context, World& world);
	void CreateShadowViews(SceneView& view, World& world);
	
	RefCountPtr<GraphicsDevice> m_pDevice;
	RefCountPtr<SwapChain> m_pSwapchain;
	std::unique_ptr<RGResourcePool> m_RenderGraphPool;

	uint32 m_Frame = 0;
	TimeHistory<float, 180> m_FrameHistory;

	RefCountPtr<Texture> m_pColorHistory;
	RefCountPtr<Texture> m_pHZB;
	RefCountPtr<Texture> m_pColorOutput;
	std::vector<RefCountPtr<Texture>> m_ShadowMaps;

	std::unique_ptr<ClusteredForward> m_pClusteredForward;
	std::unique_ptr<TiledForward> m_pTiledForward;
	std::unique_ptr<RTAO> m_pRTAO;
	std::unique_ptr<RTReflections> m_pRTReflections;
	std::unique_ptr<SSAO> m_pSSAO;
	std::unique_ptr<PathTracing> m_pPathTracing;
	std::unique_ptr<CBTTessellation> m_pCBTTessellation;
	std::unique_ptr<GpuParticles> m_pParticles;
	std::unique_ptr<Clouds> m_pClouds;
	std::unique_ptr<ShaderDebugRenderer> m_pShaderDebugRenderer;
	std::unique_ptr<GPUDrivenRenderer> m_pGPUDrivenRenderer;
	std::unique_ptr<DDGI> m_pDDGI;
	std::unique_ptr<VisualizeTexture> m_pVisualizeTexture;

	LightCull2DData m_LightCull2DData;
	LightCull3DData m_LightCull3DData;
	VolumetricFogData m_FogData;

	WindowHandle m_Window = nullptr;
	std::unique_ptr<Camera> m_pCamera;

	RenderPath m_RenderPath = RenderPath::Visibility;

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
	RefCountPtr<Texture> m_pLensDirtTexture;
	Vector3 m_LensDirtTint = Vector3::One;

	// Eye adaptation
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
	std::array<RefCountPtr<Buffer>, SwapChain::NUM_FRAMES> m_ReductionReadbackTargets;

	//Camera motion
	RefCountPtr<PipelineState> m_pCameraMotionPSO;

	RefCountPtr<PipelineState> m_pTemporalResolvePSO;

	//Sky
	RefCountPtr<PipelineState> m_pSkyboxPSO;
	RefCountPtr<PipelineState> m_pRenderSkyPSO;

	//Bloom
	RefCountPtr<PipelineState> m_pBloomDownsamplePSO;
	RefCountPtr<PipelineState> m_pBloomDownsampleKarisAveragePSO;
	RefCountPtr<PipelineState> m_pBloomUpsamplePSO;

	// Visibility buffer
	RefCountPtr<PipelineState> m_pVisibilityShadingPSO;
	RefCountPtr<PipelineState> m_pVisibilityDebugRenderPSO;
	uint32 m_VisibilityDebugRenderMode = 0;
};
