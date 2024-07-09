#pragma once

#include "RHI/RHI.h"
#include "RenderGraph/RenderGraphDefinitions.h"

struct RenderView;
struct SceneTextures;

class RTReflections
{
public:
	RTReflections(GraphicsDevice* pDevice);
	void Execute(RGGraph& graph, const RenderView* pView, SceneTextures& sceneTextures);

private:
	Ref<StateObject> m_pRtSO;
};

