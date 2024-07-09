#pragma once
#include "RHI/RHI.h"
#include "RHI/Device.h"
#include "Renderer/Light.h"
#include "Renderer/SceneView.h"
#include "RHI/CommandQueue.h"
#include "Renderer/Techniques/VolumetricFog.h"
#include "Renderer/Techniques/VisualizeTexture.h"
#include "App.h"

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
class MeshletRasterizer;
class DDGI;
class VolumetricFog;
class ForwardRenderer;
class LightCulling;
struct Material;

enum class RenderPath
{
	Tiled,
	Clustered,
	PathTracing,
	Visibility,
	VisibilityDeferred,
	MAX
};

class DemoApp : public App
{
public:
	DemoApp();
	~DemoApp();

	virtual void Init() override;
	virtual void Update() override;
	virtual void Shutdown() override;
	virtual void OnWindowResized(uint32 width, uint32 height);

private:
	void OnResizeViewport(uint32 width, uint32 height);

	void InitializePipelines();
	void SetupScene(const char* pPath);

	void UpdateImGui();

	RGTexture* ComputeBloom(RGGraph& graph, const RenderView* pView, RGTexture* pColor);
	RGBuffer* ComputeExposure(RGGraph& graph, const RenderView* pView, RGTexture* pColor);
	void MakeScreenshot();

	void CreateShadowViews(const RenderView& mainView, World& world, RenderWorld& renderWorld);

	std::unique_ptr<RGResourcePool> m_RenderGraphPool;

	uint32 m_Frame = 0;

	Ref<Texture> m_pColorHistory;
	Ref<Texture> m_pHZB;
	Ref<Texture> m_pColorOutput;
	Array<Ref<Texture>> m_ShadowMaps;
	Array<Ref<Texture>> m_ShadowHZBs;

	std::unique_ptr<VolumetricFog> m_pVolumetricFog;
	std::unique_ptr<ForwardRenderer> m_pForwardRenderer;
	std::unique_ptr<LightCulling> m_pLightCulling;
	std::unique_ptr<RTAO> m_pRTAO;
	std::unique_ptr<RTReflections> m_pRTReflections;
	std::unique_ptr<SSAO> m_pSSAO;
	std::unique_ptr<PathTracing> m_pPathTracing;
	std::unique_ptr<CBTTessellation> m_pCBTTessellation;
	std::unique_ptr<GpuParticles> m_pParticles;
	std::unique_ptr<Clouds> m_pClouds;
	std::unique_ptr<ShaderDebugRenderer> m_pShaderDebugRenderer;
	std::unique_ptr<MeshletRasterizer> m_pMeshletRasterizer;
	std::unique_ptr<DDGI> m_pDDGI;
	std::unique_ptr<CaptureTextureSystem> m_pCaptureTextureSystem;
	CaptureTextureContext m_CaptureTextureContext;

	VolumetricFogData m_FogData;

	WindowHandle m_Window = nullptr;
	std::unique_ptr<Camera> m_pCamera;

	RenderPath m_RenderPath = RenderPath::Visibility;

	World m_World;
	RenderWorld m_RenderWorld;
	RenderView m_MainView;

	//Shadow mapping
	Ref<PipelineState> m_pShadowsOpaquePSO;
	Ref<PipelineState> m_pShadowsAlphaMaskPSO;

	//Depth Prepass
	Ref<PipelineState> m_pDepthPrepassOpaquePSO;
	Ref<PipelineState> m_pDepthPrepassAlphaMaskPSO;

	//Tonemapping
	Ref<PipelineState> m_pToneMapPSO;
	Ref<Texture> m_pLensDirtTexture;
	Vector3 m_LensDirtTint = Vector3::One;

	// Eye adaptation
	Ref<Buffer> m_pAverageLuminance;
	Ref<Texture> m_pDebugHistogramTexture;
	Ref<PipelineState> m_pDownsampleColorPSO;
	Ref<PipelineState> m_pLuminanceHistogramPSO;
	Ref<PipelineState> m_pAverageLuminancePSO;
	Ref<PipelineState> m_pDrawHistogramPSO;

	//Depth Reduction
	Ref<PipelineState> m_pPrepareReduceDepthPSO;
	Ref<PipelineState> m_pPrepareReduceDepthMsaaPSO;
	Ref<PipelineState> m_pReduceDepthPSO;
	StaticArray<Ref<Buffer>, GraphicsDevice::NUM_BUFFERS> m_ReductionReadbackTargets;

	//Camera motion
	Ref<PipelineState> m_pCameraMotionPSO;

	Ref<PipelineState> m_pTemporalResolvePSO;

	//Sky
	Ref<PipelineState> m_pSkyboxPSO;
	Ref<PipelineState> m_pRenderSkyPSO;

	//Bloom
	Ref<PipelineState> m_pBloomDownsamplePSO;
	Ref<PipelineState> m_pBloomDownsampleKarisAveragePSO;
	Ref<PipelineState> m_pBloomUpsamplePSO;

	// Visibility buffer
	Ref<PipelineState> m_pVisibilityShadingGraphicsPSO;
	Ref<PipelineState> m_pVisibilityDebugRenderPSO;

	Ref<PipelineState> m_pVisibilityGBufferPSO;
	Ref<PipelineState> m_pDeferredShadePSO;
};
