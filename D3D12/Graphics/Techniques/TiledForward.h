#pragma once
#include "Graphics/RenderGraph/RenderGraphDefinitions.h"
#include "Graphics/RenderGraph/Blackboard.h"

class RootSignature;
class GraphicsDevice;
class PipelineState;
class RGGraph;

struct SceneView;
struct SceneTextures;

struct LightCull2DData
{
	RGTexture* pLightGridOpaque;
	RGTexture* pLightGridTransparant;

	RGBuffer* pLightIndexCounter;
	RGBuffer* pLightIndexListOpaque;
	RGBuffer* pLightIndexListTransparant;
};

class TiledForward
{
public:
	TiledForward(GraphicsDevice* pDevice);

	void ComputeLightCulling(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures, LightCull2DData& resources);
	void RenderBasePass(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures, const LightCull2DData& lightCullData, RGTexture* pFogTexture);

	void VisualizeLightDensity(RGGraph& graph, GraphicsDevice* pDevice, const SceneView* pView, SceneTextures& sceneTextures, const LightCull2DData& lightCullData);

private:
	GraphicsDevice* m_pDevice;
	RefCountPtr<RootSignature> m_pCommonRS;

	RefCountPtr<PipelineState> m_pComputeLightCullPSO;

	RefCountPtr<PipelineState> m_pDiffusePSO;
	RefCountPtr<PipelineState> m_pDiffuseMaskedPSO;
	RefCountPtr<PipelineState> m_pDiffuseAlphaPSO;

	RefCountPtr<PipelineState> m_pVisualizeLightsPSO;
};
