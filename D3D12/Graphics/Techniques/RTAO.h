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
	RefCountPtr<Texture> m_pHistory;

	RefCountPtr<StateObject> m_pTraceRaysSO;
	RefCountPtr<RootSignature> m_pCommonRS;
	RefCountPtr<PipelineState> m_pDenoisePSO;
	RefCountPtr<PipelineState> m_pBilateralBlurPSO;
};
