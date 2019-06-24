#pragma once
#include "Graphics/Graphics.h"
class Graphics;
class ComputePipelineState;
class RootSignature;
class GraphicsPipelineState;
class StructuredBuffer;
class Texture2D;

struct ClusteredForwardInputResources
{
	Texture2D* pDepthPrepassBuffer = nullptr;
	Texture2D* pRenderTarget = nullptr;
	const std::vector<Batch>* pOpaqueBatches;
	const std::vector<Batch>* pTransparantBatches;
};

class ClusteredForward
{
public:
	ClusteredForward(Graphics* pGraphics);

	void OnSwapchainCreated(int windowWidth, int windowHeight);

	void Execute(const ClusteredForwardInputResources& resources);

private:
	void SetupResources(Graphics* pGraphics);
	void SetupPipelines(Graphics* pGraphics);

	Graphics* m_pGraphics;

	//Step 1: AABB
	std::unique_ptr<RootSignature> m_pCreateAabbRS;
	std::unique_ptr<ComputePipelineState> m_pCreateAabbPSO;
	std::unique_ptr<StructuredBuffer> m_pAABBs;

	//Step 2: Mark Unique Clusters
	std::unique_ptr<RootSignature> m_pMarkUniqueClustersRS;
	std::unique_ptr<GraphicsPipelineState> m_pMarkUniqueClustersPSO;
	std::unique_ptr<StructuredBuffer> m_pUniqueClusters;

	std::unique_ptr<Texture2D> m_pDebugTexture;

	//Step 3: Compact Cluster List
	std::unique_ptr<RootSignature> m_pCompactClustersRS;
	std::unique_ptr<ComputePipelineState> m_pCompactClustersPSO;
	std::unique_ptr<StructuredBuffer> m_pActiveClusters;

	//Step 4: Light Culling
	std::unique_ptr<RootSignature> m_pLightCullingRS;
	std::unique_ptr<ComputePipelineState> m_pLightCullingPSO;

	//Step 5: Lighting
	std::unique_ptr<RootSignature> m_pDiffuseRS;
	std::unique_ptr<GraphicsPipelineState> m_pDiffusePSO;
};

