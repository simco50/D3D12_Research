#pragma once

#include "RHI/RHI.h"
#include "RenderGraph/RenderGraphDefinitions.h"

struct SceneView;
struct SceneTextures;

class SSAO
{
public:
	SSAO(GraphicsDevice* pDevice);

	RGTexture* Execute(RGGraph& graph, const SceneView* pView, RGTexture* pDepth);

private:
	Ref<PipelineState> m_pSSAOPSO;
	Ref<PipelineState> m_pSSAOBlurPSO;
};
