#pragma once
#include "CBT.h"

class RootSignature;
class Buffer;
class PipelineState;
class Texture;
class GraphicsDevice;
class RGGraph;
struct SceneView;
struct SceneTextures;

struct CBTData
{
	uint32 SplitMode = 0;
	RefCountPtr<Texture> pHeightmap;
	RefCountPtr<Buffer> pCBTBuffer;
	RefCountPtr<Buffer> pCBTIndirectArgs;
	RefCountPtr<Texture> pDebugVisualizeTexture;
};

class CBTTessellation
{
public:
	CBTTessellation(GraphicsDevice* pDevice);

	void Execute(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures)
	{
		Execute(graph, m_CBTData, pView, sceneTextures);
	}

	void Execute(RGGraph& graph, CBTData& data, const SceneView* pView, SceneTextures& sceneTextures);
	static void CBTDemo();

private:
	void SetupPipelines(GraphicsDevice* pDevice);
	void CreateResources(GraphicsDevice* pDevice);

	GraphicsDevice* m_pDevice;
	CBTData m_CBTData;

	RefCountPtr<RootSignature> m_pCBTRS;
	RefCountPtr<PipelineState> m_pCBTIndirectArgsPSO;
	RefCountPtr<PipelineState> m_pCBTCacheBitfieldPSO;
	RefCountPtr<PipelineState> m_pCBTSumReductionPSO;
	RefCountPtr<PipelineState> m_pCBTSumReductionFirstPassPSO;
	RefCountPtr<PipelineState> m_pCBTUpdatePSO;
	RefCountPtr<PipelineState> m_pCBTDebugVisualizePSO;
	RefCountPtr<PipelineState> m_pCBTRenderPSO;
	RefCountPtr<PipelineState> m_pCBTRenderMeshShaderPSO;
};
