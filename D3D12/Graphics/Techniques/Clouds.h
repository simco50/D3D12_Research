#pragma once
#include "Graphics/RHI/RHI.h"
#include "Graphics/RenderGraph/RenderGraphDefinitions.h"

struct SceneTextures;
struct SceneView;

class Clouds
{
public:
	Clouds(GraphicsDevice* pDevice);
	RGTexture* Render(RGGraph& graph, SceneTextures& sceneTextures, const SceneView* pView);

private:
	Ref<Texture> m_pShapeNoise;
	Ref<Texture> m_pDetailNoise;
	Ref<Texture> m_pCloudHeightDensityLUT;
};
