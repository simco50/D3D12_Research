#pragma once
#include "RHI/RHI.h"

#include "RHI/Device.h"
#include "RHI/CommandQueue.h"
#include "Renderer/RenderTypes.h"
#include "Renderer/Techniques/VisualizeTexture.h"
#include "Renderer/Techniques/ShaderDebugRenderer.h"
#include "Renderer/Techniques/VolumetricFog.h"
#include "Renderer/AccelerationStructure.h"
#include "RenderGraph/RenderGraphDefinitions.h"
#include "RenderGraph/RenderGraph.h"

class Camera;
class RTAO;
class RTReflections;
class SSAO;
class GpuParticles;
class PathTracing;
class CBTTessellation;
class Clouds;
class ShaderDebugRenderer;
class MeshletRasterizer;
class DDGI;
class VolumetricFog;
class ForwardRenderer;
class LightCulling;

class Renderer
{
public:
	Renderer();
	~Renderer();

	void Init(GraphicsDevice* pDevice, World* pWorld);
	void Shutdown();

	void Render(const Camera& camera, Texture* pTarget);
	void DrawImGui(FloatRect viewport);
	void MakeScreenshot(Texture* pSource);

	static void DrawScene(CommandContext& context, const RenderView& view, Batch::Blending blendModes);
	static void DrawScene(CommandContext& context, Span<const Batch> batches, const VisibilityMask& visibility, Batch::Blending blendModes);
	static void BindViewUniforms(CommandContext& context, const RenderView& view);
	static void BindCullViewUniforms(CommandContext& context, const RenderView& view);

	uint32 GetNumLights() const { return m_LightBuffer.Count; }
	uint32 GetFrameIndex() const { return m_Frame; }
	Span<const Batch> GetBatches() const { return m_Batches; }
	const RenderView& GetMainView() const { return m_MainView; }

private:
	void InitializePipelines();

	void GetViewUniforms(const RenderView& view, ShaderInterop::ViewUniforms& outUniforms);

	void UploadViewUniforms(CommandContext& context, RenderView& view);
	void UploadSceneData(CommandContext& context);

	void CreateShadowViews(const RenderView& mainView);

	/*-----------------------*/
	/*		TECHNIQUES		 */
	/*-----------------------*/

	UniquePtr<RGResourcePool>				m_RenderGraphPool;

	UniquePtr<VolumetricFog>				m_pVolumetricFog;
	VolumetricFogData						m_FogData;
	UniquePtr<ForwardRenderer>				m_pForwardRenderer;
	UniquePtr<LightCulling>					m_pLightCulling;
	UniquePtr<RTAO>							m_pRTAO;
	UniquePtr<RTReflections>				m_pRTReflections;
	UniquePtr<SSAO>							m_pSSAO;
	UniquePtr<PathTracing>					m_pPathTracing;
	UniquePtr<CBTTessellation>				m_pCBTTessellation;
	UniquePtr<GpuParticles>					m_pParticles;
	UniquePtr<Clouds>						m_pClouds;
	UniquePtr<ShaderDebugRenderer>			m_pShaderDebugRenderer;
	UniquePtr<MeshletRasterizer>			m_pMeshletRasterizer;
	UniquePtr<DDGI>							m_pDDGI;
	UniquePtr<CaptureTextureSystem>			m_pCaptureTextureSystem;
	CaptureTextureContext					m_CaptureTextureContext;

	Ref<Texture>							m_pColorHistory;
	Ref<Texture>							m_pHZB;
	Array<Ref<Texture>>						m_ShadowMaps;
	Array<Ref<Texture>>						m_ShadowHZBs;

	uint32									m_Frame			= 0;
	RenderPath								m_RenderPath	= RenderPath::Visibility;
	RenderView								m_MainView{};

	GraphicsDevice*							m_pDevice		= nullptr;
	World*									m_pWorld		= nullptr;
	Array<Batch>							m_Batches;

	struct SceneBuffer
	{
		uint32		Count = 0;
		Ref<Buffer> pBuffer;
	};

	AccelerationStructure					m_AccelerationStructure;
	SceneBuffer								m_LightBuffer;
	SceneBuffer								m_MaterialBuffer;
	SceneBuffer								m_MeshBuffer;
	SceneBuffer								m_InstanceBuffer;
	SceneBuffer								m_DDGIVolumesBuffer;
	SceneBuffer								m_FogVolumesBuffer;
	SceneBuffer								m_LightMatricesBuffer;
	Ref<Texture>							m_pSky;
	GPUDebugRenderData						m_DebugRenderData{};

	Array<ShadowView>						m_ShadowViews;
	Vector4									m_ShadowCascadeDepths;
	uint32									m_NumShadowCascades = 0;


	/*-----------------------*/
	/*	 SHADER PIPELINES	 */
	/*-----------------------*/

	// Shadow mapping
	Ref<PipelineState>						m_pShadowsOpaquePSO;
	Ref<PipelineState>						m_pShadowsAlphaMaskPSO;

	// Depth Prepass
	Ref<PipelineState>						m_pDepthPrepassOpaquePSO;
	Ref<PipelineState>						m_pDepthPrepassAlphaMaskPSO;

	// Tonemapping
	Ref<PipelineState>						m_pToneMapPSO;
	Ref<Texture>							m_pLensDirtTexture;
	Vector3									m_LensDirtTint = Vector3::One;

	// Eye adaptation
	Ref<Buffer>								m_pAverageLuminance;
	Ref<Texture>							m_pDebugHistogramTexture;
	Ref<PipelineState>						m_pDownsampleColorPSO;
	Ref<PipelineState>						m_pLuminanceHistogramPSO;
	Ref<PipelineState>						m_pAverageLuminancePSO;
	Ref<PipelineState>						m_pDrawHistogramPSO;

	// Depth Reduction
	Ref<PipelineState>						m_pPrepareReduceDepthPSO;
	Ref<PipelineState>						m_pReduceDepthPSO;
	StaticArray<Ref<Buffer>, GraphicsDevice::NUM_BUFFERS>	m_ReductionReadbackTargets;

	// Camera motion
	Ref<PipelineState>						m_pCameraMotionPSO;

	Ref<PipelineState>						m_pTemporalResolvePSO;

	// Sky
	Ref<PipelineState>						m_pSkyboxPSO;
	Ref<PipelineState>						m_pRenderSkyPSO;

	// Bloom
	Ref<PipelineState>						m_pBloomDownsamplePSO;
	Ref<PipelineState>						m_pBloomDownsampleKarisAveragePSO;
	Ref<PipelineState>						m_pBloomUpsamplePSO;

	// Visibility buffer
	Ref<PipelineState>						m_pVisibilityShadingGraphicsPSO;
	Ref<PipelineState>						m_pVisibilityDebugRenderPSO;

	Ref<PipelineState>						m_pVisibilityGBufferPSO;
	Ref<PipelineState>						m_pDeferredShadePSO;

	// Skinning
	Ref<PipelineState>						m_pSkinPSO;
};
