#pragma once
#include "RenderGraph/RenderGraph.h"
class Graphics;
class PipelineState;
class RootSignature;
class Texture;
class Camera;
struct Batch;
class CommandContext;
class Buffer;
class UnorderedAccessView;
class RGGraph;
struct ShadowData;

struct ClusteredForwardInputResources
{
	RGResourceHandle DepthBuffer;
	Texture* pRenderTarget = nullptr;
	Texture* pAO = nullptr;
	Texture* pShadowMap = nullptr;
	const std::vector<Batch>* pOpaqueBatches = nullptr;
	const std::vector<Batch>* pTransparantBatches = nullptr;
	Buffer* pLightBuffer = nullptr;
	Camera* pCamera = nullptr;
	ShadowData* pShadowData = nullptr;
};

class ClusteredForward
{
public:
	ClusteredForward(Graphics* pGraphics);
	~ClusteredForward();

	void OnSwapchainCreated(int windowWidth, int windowHeight);

	void Execute(RGGraph& graph, const ClusteredForwardInputResources& resources);

private:
	void SetupResources(Graphics* pGraphics);
	void SetupPipelines(Graphics* pGraphics);

	Graphics* m_pGraphics;

	uint32 m_ClusterCountX = 0;
	uint32 m_ClusterCountY = 0;

	std::unique_ptr<Texture> m_pHeatMapTexture;

	//Step 1: AABB
	std::unique_ptr<RootSignature> m_pCreateAabbRS;
	std::unique_ptr<PipelineState> m_pCreateAabbPSO;
	std::unique_ptr<Buffer> m_pAABBs;

	//Step 2: Mark Unique Clusters
	std::unique_ptr<RootSignature> m_pMarkUniqueClustersRS;
	std::unique_ptr<PipelineState> m_pMarkUniqueClustersOpaquePSO;
	std::unique_ptr<PipelineState> m_pMarkUniqueClustersTransparantPSO;
	std::unique_ptr<Buffer> m_pUniqueClusters;
	UnorderedAccessView* m_pUniqueClustersRawUAV = nullptr;

	//Step 3: Compact Cluster List
	std::unique_ptr<RootSignature> m_pCompactClustersRS;
	std::unique_ptr<PipelineState> m_pCompactClustersPSO;
	std::unique_ptr<Buffer> m_pCompactedClusters;
	UnorderedAccessView* m_pCompactedClustersRawUAV = nullptr;

	//Step 4: Update Indirect Dispatch Buffer
	std::unique_ptr<RootSignature> m_pUpdateIndirectArgumentsRS;
	std::unique_ptr<PipelineState> m_pUpdateIndirectArgumentsPSO;
	std::unique_ptr<Buffer> m_pIndirectArguments;

	//Step 5: Light Culling
	std::unique_ptr<RootSignature> m_pLightCullingRS;
	std::unique_ptr<PipelineState> m_pLightCullingPSO;
	ComPtr<ID3D12CommandSignature> m_pLightCullingCommandSignature;
	std::unique_ptr<Buffer> m_pLightIndexCounter;
	std::unique_ptr<Buffer> m_pLightIndexGrid;
	std::unique_ptr<Buffer> m_pLightGrid;
	UnorderedAccessView* m_pLightGridRawUAV = nullptr;

	//Step 6: Lighting
	std::unique_ptr<RootSignature> m_pDiffuseRS;
	std::unique_ptr<PipelineState> m_pDiffusePSO;
	std::unique_ptr<PipelineState> m_pDiffuseTransparancyPSO;

	//Cluster debug rendering
	std::unique_ptr<RootSignature> m_pDebugClustersRS;
	std::unique_ptr<PipelineState> m_pDebugClustersPSO;
	std::unique_ptr<Buffer> m_pDebugCompactedClusters;
	std::unique_ptr<Buffer> m_pDebugLightGrid;
	Matrix m_DebugClustersViewMatrix;
	bool m_DidCopyDebugClusterData = false;
};

