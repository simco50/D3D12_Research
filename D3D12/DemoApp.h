#include "Graphics/Light.h"
#include "Core/BitField.h"
#include "Graphics/Core/DescriptorHandle.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/SceneView.h"
#include "Graphics/Core/ShaderInterop.h"

class ImGuiRenderer;
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
	void OnResize(int width, int height);

private:
	void OnResizeViewport(int width, int height);
	void Present();

	void InitializePipelines();
	void SetupScene(CommandContext& context);

	void UpdateImGui();
	void UpdateTLAS(CommandContext& context);

	void LoadMesh(const std::string& filePath, CommandContext& context);

	void UploadSceneData(CommandContext& context);

	Texture* GetDepthStencil() const { return m_pDepthStencil; }
	Texture* GetCurrentRenderTarget() const { return m_pHDRRenderTarget; }

	RefCountPtr<GraphicsDevice> m_pDevice;
	RefCountPtr<SwapChain> m_pSwapchain;

	uint32 m_WindowWidth;
	uint32 m_WindowHeight;

	uint32 m_Frame = 0;
	std::array<float, 180> m_FrameTimes{};

	RefCountPtr<Texture> m_pHDRRenderTarget;
	RefCountPtr<Texture> m_pPreviousColor;
	RefCountPtr<Texture> m_pTonemapTarget;
	RefCountPtr<Texture> m_pDepthStencil;
	RefCountPtr<Texture> m_pResolvedDepthStencil;
	RefCountPtr<Texture> m_pTAASource;
	RefCountPtr<Texture> m_pVelocity;
	RefCountPtr<Texture> m_pNormals;
	std::vector<RefCountPtr<Texture>> m_ShadowMaps;

	std::unique_ptr<ImGuiRenderer> m_pImGuiRenderer;
	std::unique_ptr<ClusteredForward> m_pClusteredForward;
	std::unique_ptr<TiledForward> m_pTiledForward;
	std::unique_ptr<RTAO> m_pRTAO;
	std::unique_ptr<RTReflections> m_pRTReflections;
	std::unique_ptr<SSAO> m_pSSAO;
	std::unique_ptr<PathTracing> m_pPathTracing;
	std::unique_ptr<CBTTessellation> m_pCBTTessellation;

	WindowHandle m_Window = nullptr;
	std::unique_ptr<Camera> m_pCamera;

	struct ScreenshotRequest
	{
		uint64 Fence;
		uint32 Width;
		uint32 Height;
		uint32 RowPitch;
		RefCountPtr<Buffer> pBuffer;
	};
	std::queue<ScreenshotRequest> m_ScreenshotBuffers;
	RenderPath m_RenderPath = RenderPath::Visibility;

	std::vector<std::unique_ptr<Mesh>> m_Meshes;
	RefCountPtr<Buffer> m_pTLAS;
	RefCountPtr<Buffer> m_pTLASScratch;

	//Shadow mapping
	RefCountPtr<RootSignature> m_pShadowsRS;
	RefCountPtr<PipelineState> m_pShadowsOpaquePSO = nullptr;
	RefCountPtr<PipelineState> m_pShadowsAlphaMaskPSO = nullptr;

	//Depth Prepass
	RefCountPtr<RootSignature> m_pDepthPrepassRS;
	RefCountPtr<PipelineState> m_pDepthPrepassOpaquePSO = nullptr;
	RefCountPtr<PipelineState> m_pDepthPrepassAlphaMaskPSO = nullptr;

	//MSAA Depth resolve
	RefCountPtr<RootSignature> m_pResolveDepthRS;
	RefCountPtr<PipelineState> m_pResolveDepthPSO = nullptr;

	//Tonemapping
	RefCountPtr<Texture> m_pDownscaledColor;
	RefCountPtr<RootSignature> m_pLuminanceHistogramRS;
	RefCountPtr<PipelineState> m_pLuminanceHistogramPSO = nullptr;
	RefCountPtr<RootSignature> m_pAverageLuminanceRS;
	RefCountPtr<PipelineState> m_pAverageLuminancePSO = nullptr;
	RefCountPtr<RootSignature> m_pToneMapRS;
	RefCountPtr<PipelineState> m_pToneMapPSO = nullptr;
	RefCountPtr<PipelineState> m_pDrawHistogramPSO = nullptr;
	RefCountPtr<RootSignature> m_pDrawHistogramRS;
	RefCountPtr<Buffer> m_pLuminanceHistogram;
	RefCountPtr<Buffer> m_pAverageLuminance;
	RefCountPtr<Texture> m_pDebugHistogramTexture;

	//SSAO
	RefCountPtr<Texture> m_pAmbientOcclusion;

	//Mip generation
	RefCountPtr<PipelineState> m_pGenerateMipsPSO = nullptr;
	RefCountPtr<RootSignature> m_pGenerateMipsRS;

	//Depth Reduction
	RefCountPtr<PipelineState> m_pPrepareReduceDepthPSO = nullptr;
	RefCountPtr<PipelineState> m_pPrepareReduceDepthMsaaPSO = nullptr;
	RefCountPtr<PipelineState> m_pReduceDepthPSO = nullptr;
	RefCountPtr<RootSignature> m_pReduceDepthRS;
	std::vector<RefCountPtr<Texture>> m_ReductionTargets;
	std::vector<RefCountPtr<Buffer>> m_ReductionReadbackTargets;

	//Camera motion
	RefCountPtr<PipelineState> m_pCameraMotionPSO = nullptr;
	RefCountPtr<RootSignature> m_pCameraMotionRS;

	//TAA
	RefCountPtr<PipelineState> m_pTemporalResolvePSO = nullptr;
	RefCountPtr<RootSignature> m_pTemporalResolveRS;

	//Sky
	RefCountPtr<RootSignature> m_pSkyboxRS;
	RefCountPtr<PipelineState> m_pSkyboxPSO = nullptr;

	//Particles
	std::unique_ptr<GpuParticles> m_pParticles;

	//Light data
	RefCountPtr<Buffer> m_pMaterialBuffer;
	RefCountPtr<Buffer> m_pMeshBuffer;
	RefCountPtr<Buffer> m_pMeshInstanceBuffer;
	RefCountPtr<Buffer> m_pTransformsBuffer;
	std::vector<Light> m_Lights;
	RefCountPtr<Buffer> m_pLightBuffer;

	//Bloom
	RefCountPtr<PipelineState> m_pBloomSeparatePSO = nullptr;
	RefCountPtr<PipelineState> m_pBloomMipChainPSO = nullptr;
	RefCountPtr<RootSignature> m_pBloomRS;
	RefCountPtr<Texture> m_pBloomTexture;
	RefCountPtr<Texture> m_pBloomIntermediateTexture;
	std::vector<RefCountPtr<UnorderedAccessView>> m_pBloomUAVs;
	std::vector<RefCountPtr<UnorderedAccessView>> m_pBloomIntermediateUAVs;

	// Visibility buffer
	RefCountPtr<RootSignature> m_pVisibilityRenderingRS;
	RefCountPtr<PipelineState> m_pVisibilityRenderingPSO = nullptr;
	RefCountPtr<PipelineState> m_pVisibilityRenderingMaskedPSO = nullptr;
	RefCountPtr<Texture> m_pVisibilityTexture;
	RefCountPtr<RootSignature> m_pVisibilityShadingRS;
	RefCountPtr<PipelineState> m_pVisibilityShadingPSO = nullptr;

	Texture* m_pVisualizeTexture = nullptr;
	SceneView m_SceneData;
	bool m_CapturePix = false;
};
