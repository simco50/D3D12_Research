#pragma once
#include "Graphics/RenderGraph/RenderGraphDefinitions.h"
class Graphics;
class RootSignature;
class PipelineState;
class Texture;
class Camera;
struct Batch;
class CommandContext;
class Buffer;
class UnorderedAccessView;
class RGGraph;
struct ShadowData;

struct TiledForwardInputResources
{
	RGResourceHandle ResolvedDepthBuffer;
	RGResourceHandle DepthBuffer;
	std::vector<std::unique_ptr<Texture>>* pShadowMaps = nullptr;
	Texture* pRenderTarget = nullptr;
	Texture* pAO = nullptr;
	const std::vector<Batch>* pOpaqueBatches = nullptr;
	const std::vector<Batch>* pTransparantBatches = nullptr;
	Buffer* pLightBuffer = nullptr;
	Camera* pCamera = nullptr;
	ShadowData* pShadowData = nullptr;
};

class TiledForward
{
public:
	TiledForward(Graphics* pGraphics);

	void OnSwapchainCreated(int windowWidth, int windowHeight);

	void Execute(RGGraph& graph, const TiledForwardInputResources& resources);
	void VisualizeLightDensity(RGGraph& graph, Camera& camera, Texture* pTarget, Texture* pDepth);

private:
	void SetupResources(Graphics* pGraphics);
	void SetupPipelines(Graphics* pGraphics);

	Graphics* m_pGraphics;

	//Light Culling
	std::unique_ptr<RootSignature> m_pComputeLightCullRS;
	std::unique_ptr<PipelineState> m_pComputeLightCullPSO;
	std::unique_ptr<Buffer> m_pLightIndexCounter;
	UnorderedAccessView* m_pLightIndexCounterRawUAV = nullptr;
	std::unique_ptr<Buffer> m_pLightIndexListBufferOpaque;
	std::unique_ptr<Texture> m_pLightGridOpaque;
	std::unique_ptr<Buffer> m_pLightIndexListBufferTransparant;
	std::unique_ptr<Texture> m_pLightGridTransparant;

	//Diffuse
	std::unique_ptr<RootSignature> m_pDiffuseRS;
	std::unique_ptr<PipelineState> m_pDiffusePSO;
	std::unique_ptr<PipelineState> m_pDiffuseAlphaPSO;

	//Visualize Light Count
	std::unique_ptr<RootSignature> m_pVisualizeLightsRS;
	std::unique_ptr<PipelineState> m_pVisualizeLightsPSO;
	std::unique_ptr<Texture> m_pVisualizationIntermediateTexture;
};