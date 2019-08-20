#pragma once
#include "Graphics/Graphics.h"
#include "Graphics/Light.h"
class Graphics;
class ComputePipelineState;
class RootSignature;
class GraphicsPipelineState;
class StructuredBuffer;
class Texture2D;
class ByteAddressBuffer;
class Camera;
class CommandSignature;
class GpuParticles;

struct ClusteredForwardInputResources
{
	Texture2D* pRenderTarget = nullptr;
	const std::vector<Batch>* pOpaqueBatches;
	const std::vector<Batch>* pTransparantBatches;
	StructuredBuffer* pLightBuffer;
	Camera* pCamera;
};

class ClusteredForward
{
public:
	ClusteredForward(Graphics* pGraphics);
	~ClusteredForward();

	void OnSwapchainCreated(int windowWidth, int windowHeight);

	void Execute(const ClusteredForwardInputResources& resources);

private:
	void SetupResources(Graphics* pGraphics);
	void SetupPipelines(Graphics* pGraphics);

	Graphics* m_pGraphics;

	uint32 m_ClusterCountX = 0;
	uint32 m_ClusterCountY = 0;

	std::unique_ptr<Texture2D> m_pHeatMapTexture;
	std::unique_ptr<Texture2D> m_pDepthTexture;

	//Step 1: AABB
	std::unique_ptr<RootSignature> m_pCreateAabbRS;
	std::unique_ptr<ComputePipelineState> m_pCreateAabbPSO;
	std::unique_ptr<StructuredBuffer> m_pAABBs;

	//Step 2: Mark Unique Clusters
	std::unique_ptr<RootSignature> m_pMarkUniqueClustersRS;
	std::unique_ptr<GraphicsPipelineState> m_pMarkUniqueClustersOpaquePSO;
	std::unique_ptr<GraphicsPipelineState> m_pMarkUniqueClustersTransparantPSO;
	std::unique_ptr<StructuredBuffer> m_pUniqueClusters;

	//Step 3: Compact Cluster List
	std::unique_ptr<RootSignature> m_pCompactClustersRS;
	std::unique_ptr<ComputePipelineState> m_pCompactClustersPSO;
	std::unique_ptr<StructuredBuffer> m_pCompactedClusters;

	//Step 4: Update Indirect Dispatch Buffer
	std::unique_ptr<RootSignature> m_pUpdateIndirectArgumentsRS;
	std::unique_ptr<ComputePipelineState> m_pUpdateIndirectArgumentsPSO;
	std::unique_ptr<ByteAddressBuffer> m_pIndirectArguments;

	//Step 5: Light Culling
	std::unique_ptr<RootSignature> m_pLightCullingRS;
	std::unique_ptr<ComputePipelineState> m_pLightCullingPSO;
	std::unique_ptr<CommandSignature> m_pLightCullingCommandSignature;
	std::unique_ptr<StructuredBuffer> m_pLightIndexCounter;
	std::unique_ptr<StructuredBuffer> m_pLightIndexGrid;
	std::unique_ptr<StructuredBuffer> m_pLightGrid;

	//Alternative light culling
	std::unique_ptr<ComputePipelineState> m_pAlternativeLightCullingPSO;

	//Step 6: Lighting
	std::unique_ptr<RootSignature> m_pDiffuseRS;
	std::unique_ptr<GraphicsPipelineState> m_pDiffusePSO;
	std::unique_ptr<GraphicsPipelineState> m_pDiffuseTransparancyPSO;

	//Cluster debug rendering
	std::unique_ptr<RootSignature> m_pDebugClustersRS;
	std::unique_ptr<GraphicsPipelineState> m_pDebugClustersPSO;
	std::unique_ptr<StructuredBuffer> m_pDebugCompactedClusters;
	std::unique_ptr<StructuredBuffer> m_pDebugLightGrid;
	Matrix m_DebugClustersViewMatrix;
	bool m_DidCopyDebugClusterData = false;

	std::unique_ptr<GpuParticles> m_pGpuParticles;
};

