#pragma once

#include "RHI/RHI.h"
#include "RenderGraph/RenderGraphDefinitions.h"

struct RenderView;
struct SceneTextures;

class SSAO
{
public:
	SSAO(GraphicsDevice* pDevice);

	RGTexture* Execute(RGGraph& graph, const RenderView* pView, RGTexture* pDepth);

private:
	Ref<PipelineState> m_pSSAOPSO;
	Ref<PipelineState> m_pSSAOBlurPSO;
};
