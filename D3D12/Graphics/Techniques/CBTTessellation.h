#pragma once
#include "CBT.h"

class RootSignature;
class Buffer;
class PipelineState;
class Texture;
class GraphicsDevice;
class RGGraph;
class CommandSignature;
struct SceneView;

class CBTTessellation
{
public:
	CBTTessellation(GraphicsDevice* pDevice);

	void Execute(RGGraph& graph, Texture* pRenderTarget, Texture* pDepthTexture, const SceneView& resources);

private:
	void AllocateCBT();
	void SetupPipelines();

	void DemoCpuCBT();

	GraphicsDevice* m_pDevice;

	CBT m_CBT;
	int m_MaxDepth = 14;
	bool m_IsDirty = true;
	BoundingFrustum m_CachedFrustum;
	Matrix m_CachedViewMatrix;

	std::unique_ptr<Texture> m_pHeightmap;

	std::unique_ptr<RootSignature> m_pCBTRS;
	std::unique_ptr<Buffer> m_pCBTBuffer;
	std::unique_ptr<Buffer> m_pCBTIndirectArgs;
	std::unique_ptr<Texture> m_pDebugVisualizeTexture;
	PipelineState* m_pCBTIndirectArgsPSO = nullptr;
	PipelineState* m_pCBTSumReductionPSO = nullptr;
	PipelineState* m_pCBTUpdatePSO = nullptr;
	PipelineState* m_pCBTDebugVisualizePSO = nullptr;
	PipelineState* m_pCBTRenderPSO = nullptr;
	PipelineState* m_pCBTRenderMeshShaderPSO = nullptr;
};
