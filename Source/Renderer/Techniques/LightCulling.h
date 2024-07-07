#pragma once
#include "RHI/RHI.h"
#include "Renderer/RenderGraph/RenderGraphDefinitions.h"
#include "Renderer/SceneView.h"

struct SceneView;
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

	void ComputeClusteredLightCulling(RGGraph& graph, const SceneView* pView, LightCull3DData& resources);
	void ComputeTiledLightCulling(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures, LightCull2DData& resources);

	RGTexture* VisualizeLightDensity(RGGraph& graph, const SceneView* pView, RGTexture* pSceneDepth, const LightCull3DData& lightCullData);
	RGTexture* VisualizeLightDensity(RGGraph& graph, const SceneView* pView, RGTexture* pSceneDepth, const LightCull2DData& lightCullData);

private:
	GraphicsDevice* m_pDevice;

	// Clustered
	Ref<PipelineState> m_pClusteredCullPSO;
	Ref<PipelineState> m_pTiledVisualizeLightsPSO;

	// Tiled
	Ref<PipelineState> m_pTiledCullPSO;
	Ref<PipelineState> m_pClusteredVisualizeLightsPSO;
};
