#pragma once

#include "Graphics/RHI/RHI.h"
#include "Graphics/RenderGraph/RenderGraphDefinitions.h"

struct SceneView;
struct SceneTextures;

class SSAO
{
public:
	SSAO(GraphicsDevice* pDevice);

	RGTexture* Execute(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures);

private:
	RefCountPtr<RootSignature> m_pSSAORS;
	RefCountPtr<PipelineState> m_pSSAOPSO;
	RefCountPtr<PipelineState> m_pSSAOBlurPSO;
};
