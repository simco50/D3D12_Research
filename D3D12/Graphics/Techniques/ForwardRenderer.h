#pragma once
#include "Graphics/RHI/RHI.h"
#include "Graphics/RenderGraph/RenderGraphDefinitions.h"
#include "Graphics/SceneView.h"

struct SceneView;
struct SceneTextures;
struct LightCull3DData;
struct LightCull2DData;

class ForwardRenderer
{
public:
	ForwardRenderer(GraphicsDevice* pDevice);
	~ForwardRenderer();

	void RenderForwardClustered(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures, const LightCull3DData& lightCullData, RGTexture* pFogTexture, bool translucentOnly = false);
	void RenderForwardTiled(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures, const LightCull2DData& lightCullData, RGTexture* pFogTexture);

private:
	GraphicsDevice* m_pDevice;
	Ref<RootSignature> m_pForwardRS;

	// Clustered
	Ref<PipelineState> m_pClusteredForwardPSO;
	Ref<PipelineState> m_pClusteredForwardMaskedPSO;
	Ref<PipelineState> m_pClusteredForwardAlphaBlendPSO;

	// Tiled
	Ref<PipelineState> m_pTiledForwardPSO;
	Ref<PipelineState> m_pTiledForwardMaskedPSO;
	Ref<PipelineState> m_pTiledForwardAlphaBlendPSO;
};
