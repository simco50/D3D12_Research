#pragma once
#include "Graphics/RenderGraph/RenderGraphDefinitions.h"

class GraphicsDevice;
class PipelineState;
class RootSignature;
class Texture;
class Camera;
class CommandSignature;
class Buffer;
class UnorderedAccessView;
class RGGraph;
struct SceneView;

class ClusteredForward
{
public:
	ClusteredForward(GraphicsDevice* pDevice);
	~ClusteredForward();

	void OnResize(int windowWidth, int windowHeight);

	void Execute(RGGraph& graph, const SceneView& resources);
	void VisualizeLightDensity(RGGraph& graph, Camera& camera, Texture* pTarget, Texture* pDepth);

private:
	void SetupPipelines();

	GraphicsDevice* m_pDevice;

	uint32 m_ClusterCountX = 0;
	uint32 m_ClusterCountY = 0;

	std::unique_ptr<Texture> m_pHeatMapTexture;

	// AABB
	std::unique_ptr<RootSignature> m_pCreateAabbRS;
	PipelineState* m_pCreateAabbPSO = nullptr;
	std::unique_ptr<Buffer> m_pAABBs;

	// Light Culling
	std::unique_ptr<RootSignature> m_pLightCullingRS;
	PipelineState* m_pLightCullingPSO = nullptr;
	std::unique_ptr<CommandSignature> m_pLightCullingCommandSignature;
	std::unique_ptr<Buffer> m_pLightIndexGrid;
	std::unique_ptr<Buffer> m_pLightGrid;
	UnorderedAccessView* m_pLightGridRawUAV = nullptr;

	// Lighting
	std::unique_ptr<RootSignature> m_pDiffuseRS;
	PipelineState* m_pDiffusePSO = nullptr;
	PipelineState* m_pDiffuseMaskedPSO = nullptr;
	PipelineState* m_pDiffuseTransparancyPSO = nullptr;

	PipelineState* m_pMeshShaderDiffusePSO = nullptr;
	PipelineState* m_pMeshShaderDiffuseMaskedPSO = nullptr;
	PipelineState* m_pMeshShaderDiffuseTransparancyPSO = nullptr;

	//Cluster debug rendering
	std::unique_ptr<RootSignature> m_pVisualizeLightClustersRS;
	PipelineState* m_pVisualizeLightClustersPSO = nullptr;
	std::unique_ptr<Buffer> m_pDebugLightGrid;
	Matrix m_DebugClustersViewMatrix;
	bool m_DidCopyDebugClusterData = false;

	//Visualize Light Count
	std::unique_ptr<RootSignature> m_pVisualizeLightsRS;
	PipelineState* m_pVisualizeLightsPSO = nullptr;
	std::unique_ptr<Texture> m_pVisualizationIntermediateTexture;

	//Volumetric Fog
	std::unique_ptr<Texture> m_pLightScatteringVolume[2];
	std::unique_ptr<Texture> m_pFinalVolumeFog;
	std::unique_ptr<RootSignature> m_pVolumetricLightingRS;
	PipelineState* m_pInjectVolumeLightPSO = nullptr;
	PipelineState* m_pAccumulateVolumeLightPSO = nullptr;

	bool m_ViewportDirty = true;
};

