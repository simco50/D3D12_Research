#pragma once

#include "RHI/RHI.h"
#include "RenderGraph/RenderGraphDefinitions.h"

struct SceneView;
struct SceneTextures;

class RTReflections
{
public:
	RTReflections(GraphicsDevice* pDevice);
	void Execute(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures);

private:
	Ref<StateObject> m_pRtSO;
};
