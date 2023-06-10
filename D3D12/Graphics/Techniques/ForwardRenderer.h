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
	RefCountPtr<RootSignature> m_pForwardRS;

	// Clustered
	RefCountPtr<PipelineState> m_pClusteredForwardPSO;
	RefCountPtr<PipelineState> m_pClusteredForwardMaskedPSO;
	RefCountPtr<PipelineState> m_pClusteredForwardAlphaBlendPSO;

	// Tiled
	RefCountPtr<PipelineState> m_pTiledForwardPSO;
	RefCountPtr<PipelineState> m_pTiledForwardMaskedPSO;
	RefCountPtr<PipelineState> m_pTiledForwardAlphaBlendPSO;
};
