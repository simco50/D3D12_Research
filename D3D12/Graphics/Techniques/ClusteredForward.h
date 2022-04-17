#pragma once
class GraphicsDevice;
class PipelineState;
class RootSignature;
class Texture;
class CommandSignature;
class Buffer;
class UnorderedAccessView;
class RGGraph;
struct SceneView;
struct SceneTextures;

struct ClusteredLightCullData
{
	IntVector3 ClusterCount;
	RefCountPtr<Buffer> pAABBs;
	RefCountPtr<Buffer> pLightIndexGrid;
	RefCountPtr<Buffer> pLightGrid;
	RefCountPtr<UnorderedAccessView> pLightGridRawUAV;
	Vector2 LightGridParams;
	bool IsViewDirty = true;

	RefCountPtr<Buffer> pDebugLightGrid;
	Matrix DebugClustersViewMatrix;
	bool DirtyDebugData = true;
};

struct VolumetricFogData
{
	RefCountPtr<Texture> pLightScatteringVolume[2];
	RefCountPtr<Texture> pFinalVolumeFog;
};

class ClusteredForward
{
public:
	ClusteredForward(GraphicsDevice* pDevice);
	~ClusteredForward();

	void OnResize(int windowWidth, int windowHeight);

	void CreateLightCullingResources(ClusteredLightCullData& resources, const IntVector2& viewDimensions);
	void ComputeLightCulling(RGGraph& graph, const SceneView& view, ClusteredLightCullData& resources);
	void VisualizeClusters(RGGraph& graph, const SceneView& view, const SceneTextures& sceneTextures, ClusteredLightCullData& resources);

	void CreateVolumetricFogResources(VolumetricFogData& resources, const IntVector2& viewDimensions);
	void RenderVolumetricFog(RGGraph& graph, const SceneView& view, const ClusteredLightCullData& cullData, VolumetricFogData& fogData);

	void RenderBasePass(RGGraph& graph, const SceneView& view, const SceneTextures& sceneTextures, const ClusteredLightCullData& lightCullData, Texture* pFogTexture);

	void Execute(RGGraph& graph, const SceneView& view, const SceneTextures& sceneTextures);
	void VisualizeLightDensity(RGGraph& graph, const SceneView& view, const SceneTextures& sceneTextures);

private:
	GraphicsDevice* m_pDevice;

	RefCountPtr<Texture> m_pHeatMapTexture;

	ClusteredLightCullData m_LightCullData;
	VolumetricFogData m_VolumetricFogData;

	// AABB
	RefCountPtr<PipelineState> m_pCreateAabbPSO;
	// Light Culling
	RefCountPtr<RootSignature> m_pLightCullingRS;
	RefCountPtr<PipelineState> m_pLightCullingPSO;
	RefCountPtr<CommandSignature> m_pLightCullingCommandSignature;

	// Lighting
	RefCountPtr<RootSignature> m_pDiffuseRS;
	RefCountPtr<PipelineState> m_pDiffusePSO;
	RefCountPtr<PipelineState> m_pDiffuseMaskedPSO;
	RefCountPtr<PipelineState> m_pDiffuseTransparancyPSO;

	RefCountPtr<PipelineState> m_pMeshShaderDiffusePSO;
	RefCountPtr<PipelineState> m_pMeshShaderDiffuseMaskedPSO;
	RefCountPtr<PipelineState> m_pMeshShaderDiffuseTransparancyPSO;

	//Cluster debug rendering
	RefCountPtr<RootSignature> m_pVisualizeLightClustersRS;
	RefCountPtr<PipelineState> m_pVisualizeLightClustersPSO;

	//Visualize Light Count
	RefCountPtr<RootSignature> m_pVisualizeLightsRS;
	RefCountPtr<PipelineState> m_pVisualizeLightsPSO;
	RefCountPtr<Texture> m_pVisualizationIntermediateTexture;

	//Volumetric Fog
	RefCountPtr<RootSignature> m_pVolumetricLightingRS;
	RefCountPtr<PipelineState> m_pInjectVolumeLightPSO;
	RefCountPtr<PipelineState> m_pAccumulateVolumeLightPSO;
};

