#pragma once
#include "Graphics/RHI/RHI.h"
#include "Graphics/RenderGraph/RenderGraphDefinitions.h"

struct SceneView;
struct SceneTextures;

struct LightCull3DData
{
	Vector3i ClusterCount;
	RGBuffer* pAABBs;
	RGBuffer* pLightIndexGrid;
	RGBuffer* pLightGrid;
	uint32 ClusterSize;

	Vector2 LightGridParams;

	RefCountPtr<Buffer> pDebugLightGrid;
	Matrix DebugClustersViewMatrix;
	bool DirtyDebugData = true;
};

struct VolumetricFogData
{
	RefCountPtr<Texture> pFogHistory;
};

class ClusteredForward
{
public:
	ClusteredForward(GraphicsDevice* pDevice);
	~ClusteredForward();

	void ComputeLightCulling(RGGraph& graph, const SceneView* pView, LightCull3DData& resources);
	void VisualizeClusters(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures, LightCull3DData& resources);

	RGTexture* RenderVolumetricFog(RGGraph& graph, const SceneView* pView, const LightCull3DData& cullData, VolumetricFogData& fogData);

	void RenderBasePass(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures, const LightCull3DData& lightCullData, RGTexture* pFogTexture);

	void VisualizeLightDensity(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures, const LightCull3DData& lightCullData);

private:
	GraphicsDevice* m_pDevice;

	RefCountPtr<Texture> m_pHeatMapTexture;

	// AABB
	RefCountPtr<PipelineState> m_pCreateAabbPSO;
	// Light Culling
	RefCountPtr<PipelineState> m_pLightCullingPSO;

	// Lighting
	RefCountPtr<RootSignature> m_pDiffuseRS;
	RefCountPtr<PipelineState> m_pDiffusePSO;
	RefCountPtr<PipelineState> m_pDiffuseMaskedPSO;
	RefCountPtr<PipelineState> m_pDiffuseTransparancyPSO;

	RefCountPtr<PipelineState> m_pMeshShaderDiffusePSO;
	RefCountPtr<PipelineState> m_pMeshShaderDiffuseMaskedPSO;
	RefCountPtr<PipelineState> m_pMeshShaderDiffuseTransparancyPSO;

	//Cluster debug rendering
	RefCountPtr<PipelineState> m_pVisualizeLightClustersPSO;

	//Visualize Light Count
	RefCountPtr<RootSignature> m_pCommonRS;
	RefCountPtr<PipelineState> m_pVisualizeLightsPSO;

	//Volumetric Fog
	RefCountPtr<PipelineState> m_pInjectVolumeLightPSO;
	RefCountPtr<PipelineState> m_pAccumulateVolumeLightPSO;
};
