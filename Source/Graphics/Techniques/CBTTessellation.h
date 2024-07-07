#pragma once
#include "RHI/RHI.h"
#include "Graphics/RenderGraph/RenderGraphDefinitions.h"

struct SceneView;
struct SceneTextures;

struct CBTData
{
	uint32 SplitMode = 0;
	RGBuffer* pCBT = nullptr;

	Ref<Buffer> pCBTBuffer;
	Ref<Buffer> pCBTIndirectArgs;
	Ref<Texture> pDebugVisualizeTexture;
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

	Ref<RootSignature> m_pCBTRS;
	Ref<PipelineState> m_pCBTIndirectArgsPSO;
	Ref<PipelineState> m_pCBTCacheBitfieldPSO;
	Ref<PipelineState> m_pCBTSumReductionPSO;
	Ref<PipelineState> m_pCBTUpdatePSO;
	Ref<PipelineState> m_pCBTDebugVisualizePSO;
	Ref<PipelineState> m_pCBTRenderPSO;
	Ref<PipelineState> m_pCBTShadePSO;
	Ref<PipelineState> m_pCBTRenderMeshShaderPSO;
};
