#pragma once
#include "Graphics/RenderGraph/RenderGraphDefinitions.h"

class Graphics;
class PipelineState;
class RootSignature;
class Texture;
class Camera;
class CommandSignature;
class CommandContext;
class Buffer;
class UnorderedAccessView;
class RGGraph;

struct SceneData;
struct Batch;
struct ShadowData;

class ClusteredForward
{
public:
	ClusteredForward(Graphics* pGraphics);
	~ClusteredForward();

	void OnSwapchainCreated(int windowWidth, int windowHeight);

	void Execute(RGGraph& graph, const SceneData& resources);
	void VisualizeLightDensity(RGGraph& graph, Camera& camera, Texture* pTarget, Texture* pDepth);

private:
	void SetupResources(Graphics* pGraphics);
	void SetupPipelines(Graphics* pGraphics);

	Graphics* m_pGraphics;

	uint32 m_ClusterCountX = 0;
	uint32 m_ClusterCountY = 0;

	std::unique_ptr<Texture> m_pHeatMapTexture;

	//Step 1: AABB
	std::unique_ptr<RootSignature> m_pCreateAabbRS;
	PipelineState* m_pCreateAabbPSO = nullptr;
	std::unique_ptr<Buffer> m_pAABBs;

	//Step 2: Mark Unique Clusters
	std::unique_ptr<RootSignature> m_pMarkUniqueClustersRS;
	PipelineState* m_pMarkUniqueClustersOpaquePSO = nullptr;
	PipelineState* m_pMarkUniqueClustersTransparantPSO = nullptr;
	std::unique_ptr<Buffer> m_pUniqueClusters;
	UnorderedAccessView* m_pUniqueClustersRawUAV = nullptr;

	//Step 3: Compact Cluster List
	std::unique_ptr<RootSignature> m_pCompactClustersRS;
	PipelineState* m_pCompactClustersPSO = nullptr;
	std::unique_ptr<Buffer> m_pCompactedClusters;
	UnorderedAccessView* m_pCompactedClustersRawUAV = nullptr;

	//Step 4: Update Indirect Dispatch Buffer
	std::unique_ptr<RootSignature> m_pUpdateIndirectArgumentsRS;
	PipelineState* m_pUpdateIndirectArgumentsPSO = nullptr;
	std::unique_ptr<Buffer> m_pIndirectArguments;

	//Step 5: Light Culling
	std::unique_ptr<RootSignature> m_pLightCullingRS;
	PipelineState* m_pLightCullingPSO = nullptr;
	std::unique_ptr<CommandSignature> m_pLightCullingCommandSignature;
	std::unique_ptr<Buffer> m_pLightIndexCounter;
	std::unique_ptr<Buffer> m_pLightIndexGrid;
	std::unique_ptr<Buffer> m_pLightGrid;
	UnorderedAccessView* m_pLightGridRawUAV = nullptr;

	//Step 6: Lighting
	std::unique_ptr<RootSignature> m_pDiffuseRS;
	PipelineState* m_pDiffusePSO = nullptr;
	PipelineState* m_pDiffuseTransparancyPSO = nullptr;

	//Cluster debug rendering
	std::unique_ptr<RootSignature> m_pDebugClustersRS;
	PipelineState* m_pDebugClustersPSO = nullptr;
	std::unique_ptr<Buffer> m_pDebugCompactedClusters;
	std::unique_ptr<Buffer> m_pDebugLightGrid;
	Matrix m_DebugClustersViewMatrix;
	bool m_DidCopyDebugClusterData = false;

	//Visualize Light Count
	std::unique_ptr<RootSignature> m_pVisualizeLightsRS;
	PipelineState* m_pVisualizeLightsPSO = nullptr;
	std::unique_ptr<Texture> m_pVisualizationIntermediateTexture;

	//Volumetric Fog
	std::unique_ptr<Texture> m_pLightScatteringVolume;
	std::unique_ptr<Texture> m_pFinalVolumeFog;
	std::unique_ptr<RootSignature> m_pVolumetricLightingRS;
	PipelineState* m_pInjectVolumeLightPSO = nullptr;
	PipelineState* m_pAccumulateVolumeLightPSO = nullptr;

	bool m_ViewportDirty = true;
};

