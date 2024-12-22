#pragma once
#include "RHI/RHI.h"
#include "RenderGraph/RenderGraphDefinitions.h"
#include "Renderer/Renderer.h"

struct RenderView;
struct SceneTextures;

struct LightCull3DData
{
	Vector3i ClusterCount;
	RGBuffer* pLightGrid;
	uint32 ClusterSize;

	Vector2 LightGridParams;

	Matrix DebugClustersViewMatrix;
	bool DirtyDebugData = true;
};

struct LightCull2DData
{
	RGBuffer* pLightListOpaque;
	RGBuffer* pLightListTransparent;
};

class LightCulling
{
public:
	LightCulling(GraphicsDevice* pDevice);
	~LightCulling();

	void ComputeClusteredLightCulling(RGGraph& graph, const RenderView* pView, LightCull3DData& resources);
	void ComputeTiledLightCulling(RGGraph& graph, const RenderView* pView, SceneTextures& sceneTextures, LightCull2DData& resources);

	RGTexture* VisualizeLightDensity(RGGraph& graph, const RenderView* pView, RGTexture* pSceneDepth, const LightCull3DData& lightCullData);
	RGTexture* VisualizeLightDensity(RGGraph& graph, const RenderView* pView, RGTexture* pSceneDepth, const LightCull2DData& lightCullData);

private:
	RGTexture* VisualizeLightDensity(RGGraph& graph, const RenderView* pView, RGTexture* pSceneDepth, const LightCull2DData* pLightCull2DData, const LightCull3DData* pLightCull3DData);
	GraphicsDevice* m_pDevice;

	// Clustered
	Ref<PipelineState> m_pClusteredCullPSO;
	Ref<PipelineState> m_pClusteredVisualizeLightsPSO;
	Ref<PipelineState> m_pClusteredVisualizeTopDownPSO;

	// Tiled
	Ref<PipelineState> m_pTiledCullPSO;
	Ref<PipelineState> m_pTiledVisualizeLightsPSO;
	Ref<PipelineState> m_pTiledVisualizeTopDownPSO;
};
