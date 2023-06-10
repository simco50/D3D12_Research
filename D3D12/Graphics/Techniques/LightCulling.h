#pragma once
#include "Graphics/RHI/RHI.h"
#include "Graphics/RenderGraph/RenderGraphDefinitions.h"
#include "Graphics/SceneView.h"

struct SceneView;
struct SceneTextures;

struct LightCull3DData
{
	Vector3i ClusterCount;
	RGBuffer* pLightIndexGrid;
	RGBuffer* pLightGrid;
	uint32 ClusterSize;

	Vector2 LightGridParams;

	RefCountPtr<Buffer> pDebugLightGrid;
	Matrix DebugClustersViewMatrix;
	bool DirtyDebugData = true;
};

struct LightCull2DData
{
	RGTexture* pLightGridOpaque;
	RGTexture* pLightGridTransparant;

	RGBuffer* pLightIndexCounter;
	RGBuffer* pLightIndexListOpaque;
	RGBuffer* pLightIndexListTransparant;
};

class LightCulling
{
public:
	LightCulling(GraphicsDevice* pDevice);
	~LightCulling();

	void ComputeClusteredLightCulling(RGGraph& graph, const SceneView* pView, LightCull3DData& resources);
	void ComputeTiledLightCulling(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures, LightCull2DData& resources);

	void VisualizeLightDensity(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures, const LightCull3DData& lightCullData);
	void VisualizeLightDensity(RGGraph& graph, GraphicsDevice* pDevice, const SceneView* pView, SceneTextures& sceneTextures, const LightCull2DData& lightCullData);

private:
	GraphicsDevice* m_pDevice;
	RefCountPtr<RootSignature> m_pCommonRS;

	// Clustered
	RefCountPtr<PipelineState> m_pClusteredCullPSO;
	RefCountPtr<PipelineState> m_pTiledVisualizeLightsPSO;

	// Tiled
	RefCountPtr<PipelineState> m_pTiledCullPSO;
	RefCountPtr<PipelineState> m_pClusteredVisualizeLightsPSO;
};
