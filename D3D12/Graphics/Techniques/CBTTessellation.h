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
struct SceneTextures;

class CBTTessellation
{
public:
	CBTTessellation(GraphicsDevice* pDevice);

	void Execute(RGGraph& graph, const SceneView& resources, const SceneTextures& sceneTextures);

private:
	void AllocateCBT();
	void SetupPipelines();
	void CreateResources();

	void DemoCpuCBT();

	GraphicsDevice* m_pDevice;

	CBT m_CBT;
	bool m_IsDirty = true;
	BoundingFrustum m_CachedFrustum;
	Matrix m_CachedViewMatrix;
	uint32 m_SplitMode = 0;

	RefCountPtr<Texture> m_pHeightmap;

	RefCountPtr<RootSignature> m_pCBTRS;
	RefCountPtr<Buffer> m_pCBTBuffer;
	RefCountPtr<Buffer> m_pCBTIndirectArgs;
	RefCountPtr<Texture> m_pDebugVisualizeTexture;
	RefCountPtr<PipelineState> m_pCBTIndirectArgsPSO;
	RefCountPtr<PipelineState> m_pCBTCacheBitfieldPSO;
	RefCountPtr<PipelineState> m_pCBTSumReductionPSO;
	RefCountPtr<PipelineState> m_pCBTSumReductionFirstPassPSO;
	RefCountPtr<PipelineState> m_pCBTUpdatePSO;
	RefCountPtr<PipelineState> m_pCBTDebugVisualizePSO;
	RefCountPtr<PipelineState> m_pCBTRenderPSO;
	RefCountPtr<PipelineState> m_pCBTRenderMeshShaderPSO;
};
