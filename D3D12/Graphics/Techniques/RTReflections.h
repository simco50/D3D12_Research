#pragma once

#include "Graphics/RHI/RHI.h"
#include "Graphics/RenderGraph/RenderGraphDefinitions.h"

struct SceneView;
struct SceneTextures;

class RTReflections
{
public:
	RTReflections(GraphicsDevice* pDevice);
	void Execute(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures);

private:
	Ref<StateObject> m_pRtSO;
	Ref<RootSignature> m_pGlobalRS;
};

