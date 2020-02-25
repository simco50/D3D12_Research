#pragma once
class Graphics;
class ComputePipelineState;
class RootSignature;
class GraphicsPipelineState;
class Texture;
class Camera;
struct Batch;
class CommandContext;
class Buffer;
class UnorderedAccessView;

struct ClusteredForwardInputResources
{
	Texture* pRenderTarget = nullptr;
	const std::vector<Batch>* pOpaqueBatches;
	const std::vector<Batch>* pTransparantBatches;
	Buffer* pLightBuffer;
	Camera* pCamera;
};

class ClusteredForward
{
public:
	ClusteredForward(Graphics* pGraphics);

	void OnSwapchainCreated(int windowWidth, int windowHeight);

	void Execute(CommandContext* pContext, const ClusteredForwardInputResources& resources);

private:
	void SetupResources(Graphics* pGraphics);
	void SetupPipelines(Graphics* pGraphics);

	Graphics* m_pGraphics;

	uint32 m_ClusterCountX = 0;
	uint32 m_ClusterCountY = 0;

	std::unique_ptr<Texture> m_pHeatMapTexture;
	std::unique_ptr<Texture> m_pDepthTexture;

	//Step 1: AABB
	std::unique_ptr<RootSignature> m_pCreateAabbRS;
	std::unique_ptr<ComputePipelineState> m_pCreateAabbPSO;
	std::unique_ptr<Buffer> m_pAABBs;

	//Step 2: Mark Unique Clusters
	std::unique_ptr<RootSignature> m_pMarkUniqueClustersRS;
	std::unique_ptr<GraphicsPipelineState> m_pMarkUniqueClustersOpaquePSO;
	std::unique_ptr<GraphicsPipelineState> m_pMarkUniqueClustersTransparantPSO;
	std::unique_ptr<Buffer> m_pUniqueClusters;
	UnorderedAccessView* m_pUniqueClustersRawUAV = nullptr;

	//Step 3: Compact Cluster List
	std::unique_ptr<RootSignature> m_pCompactClustersRS;
	std::unique_ptr<ComputePipelineState> m_pCompactClustersPSO;
	std::unique_ptr<Buffer> m_pCompactedClusters;
	UnorderedAccessView* m_pCompactedClustersRawUAV = nullptr;

	//Step 4: Update Indirect Dispatch Buffer
	std::unique_ptr<RootSignature> m_pUpdateIndirectArgumentsRS;
	std::unique_ptr<ComputePipelineState> m_pUpdateIndirectArgumentsPSO;
	std::unique_ptr<Buffer> m_pIndirectArguments;

	//Step 5: Light Culling
	std::unique_ptr<RootSignature> m_pLightCullingRS;
	std::unique_ptr<ComputePipelineState> m_pLightCullingPSO;
	ComPtr<ID3D12CommandSignature> m_pLightCullingCommandSignature;
	std::unique_ptr<Buffer> m_pLightIndexCounter;
	std::unique_ptr<Buffer> m_pLightIndexGrid;
	std::unique_ptr<Buffer> m_pLightGrid;
	UnorderedAccessView* m_pLightGridRawUAV = nullptr;

	//Alternative light culling
	std::unique_ptr<ComputePipelineState> m_pAlternativeLightCullingPSO;

	//Step 6: Lighting
	std::unique_ptr<RootSignature> m_pDiffuseRS;
	std::unique_ptr<GraphicsPipelineState> m_pDiffusePSO;
	std::unique_ptr<GraphicsPipelineState> m_pDiffuseTransparancyPSO;

	//Cluster debug rendering
	std::unique_ptr<RootSignature> m_pDebugClustersRS;
	std::unique_ptr<GraphicsPipelineState> m_pDebugClustersPSO;
	std::unique_ptr<Buffer> m_pDebugCompactedClusters;
	std::unique_ptr<Buffer> m_pDebugLightGrid;
	Matrix m_DebugClustersViewMatrix;
	bool m_DidCopyDebugClusterData = false;
};

