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

class ClusteredForward
{
public:
	ClusteredForward(GraphicsDevice* pDevice);
	~ClusteredForward();

	void ComputeLightCulling(RGGraph& graph, const SceneView* pView, LightCull3DData& resources);

	void RenderBasePass(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures, const LightCull3DData& lightCullData, RGTexture* pFogTexture, bool translucentOnly = false);

	void VisualizeLightDensity(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures, const LightCull3DData& lightCullData);

private:
	GraphicsDevice* m_pDevice;

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

	//Visualize Light Count
	RefCountPtr<RootSignature> m_pCommonRS;
	RefCountPtr<PipelineState> m_pVisualizeLightsPSO;
};
