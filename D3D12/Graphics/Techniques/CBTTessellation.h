#pragma once
#include "Graphics/RHI/RHI.h"
#include "Graphics/RenderGraph/RenderGraphDefinitions.h"

struct SceneView;
struct SceneTextures;

struct CBTData
{
	uint32 SplitMode = 0;
	RGBuffer* pCBT = nullptr;

	RefCountPtr<Buffer> pCBTBuffer;
	RefCountPtr<Buffer> pCBTIndirectArgs;
	RefCountPtr<Texture> pDebugVisualizeTexture;
};

class CBTTessellation
{
public:
	CBTTessellation(GraphicsDevice* pDevice);

	void RasterMain(RGGraph& graph, const SceneView* pView, const SceneTextures& sceneTextures);
	void Shade(RGGraph& graph, const SceneView* pView, const SceneTextures& sceneTextures, RGTexture* pFog);
	static void CBTDemo();

private:
	void SetupPipelines(GraphicsDevice* pDevice);

	GraphicsDevice* m_pDevice;
	CBTData m_CBTData;

	RefCountPtr<RootSignature> m_pCBTRS;
	RefCountPtr<PipelineState> m_pCBTIndirectArgsPSO;
	RefCountPtr<PipelineState> m_pCBTCacheBitfieldPSO;
	RefCountPtr<PipelineState> m_pCBTSumReductionPSO;
	RefCountPtr<PipelineState> m_pCBTUpdatePSO;
	RefCountPtr<PipelineState> m_pCBTDebugVisualizePSO;
	RefCountPtr<PipelineState> m_pCBTRenderPSO;
	RefCountPtr<PipelineState> m_pCBTShadePSO;
	RefCountPtr<PipelineState> m_pCBTRenderMeshShaderPSO;
};
