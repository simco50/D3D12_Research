#pragma once
#include "Graphics/RHI/RHI.h"
#include "Graphics/RHI/Graphics.h"
#include "Graphics/Light.h"
#include "Graphics/SceneView.h"
#include "Graphics/RHI/CommandQueue.h"
#include "Graphics/Techniques/VolumetricFog.h"
#include "App.h"

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
class MeshletRasterizer;
class DDGI;
class VisualizeTexture;
class VolumetricFog;
class ForwardRenderer;
class LightCulling;
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
	void SetupScene();

	void UpdateImGui();

	void LoadMesh(const std::string& filePath, World& world);
	void CreateShadowViews(SceneView& view, World& world);
	
	std::unique_ptr<RGResourcePool> m_RenderGraphPool;

	uint32 m_Frame = 0;

	RefCountPtr<Texture> m_pColorHistory;
	RefCountPtr<Texture> m_pHZB;
	RefCountPtr<Texture> m_pColorOutput;
	std::vector<RefCountPtr<Texture>> m_ShadowMaps;
	std::vector<RefCountPtr<Texture>> m_ShadowHZBs;

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
	std::unique_ptr<VisualizeTexture> m_pVisualizeTexture;

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

	//Tonemapping
	RefCountPtr<PipelineState> m_pToneMapPSO;
	RefCountPtr<Texture> m_pLensDirtTexture;
	Vector3 m_LensDirtTint = Vector3::One;

	// Eye adaptation
	RefCountPtr<Buffer> m_pAverageLuminance;
	RefCountPtr<Texture> m_pDebugHistogramTexture;
	RefCountPtr<PipelineState> m_pDownsampleColorPSO;
	RefCountPtr<PipelineState> m_pLuminanceHistogramPSO;
	RefCountPtr<PipelineState> m_pAverageLuminancePSO;
	RefCountPtr<PipelineState> m_pDrawHistogramPSO;

	//Depth Reduction
	RefCountPtr<PipelineState> m_pPrepareReduceDepthPSO;
	RefCountPtr<PipelineState> m_pPrepareReduceDepthMsaaPSO;
	RefCountPtr<PipelineState> m_pReduceDepthPSO;
	std::array<RefCountPtr<Buffer>, GraphicsDevice::NUM_BUFFERS> m_ReductionReadbackTargets;

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
	RefCountPtr<PipelineState> m_pVisibilityShadingGraphicsPSO;
	RefCountPtr<PipelineState> m_pVisibilityDebugRenderPSO;
	uint32 m_VisibilityDebugRenderMode = 0;
};
