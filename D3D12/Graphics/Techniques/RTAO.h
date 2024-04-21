#pragma once

#include "Graphics/RHI/RHI.h"
#include "Graphics/RenderGraph/RenderGraphDefinitions.h"

struct SceneView;
struct SceneTextures;

class RTAO
{
public:
	RTAO(GraphicsDevice* pDevice);

	RGTexture* Execute(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures);

private:
	Ref<Texture> m_pHistory;

	Ref<StateObject> m_pTraceRaysSO;
	Ref<PipelineState> m_pDenoisePSO;
	Ref<PipelineState> m_pBilateralBlurPSO;
};
