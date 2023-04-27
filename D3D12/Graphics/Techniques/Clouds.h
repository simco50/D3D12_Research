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
	RefCountPtr<Texture> m_pShapeNoise;
	RefCountPtr<Texture> m_pDetailNoise;
	RefCountPtr<Texture> m_pCloudHeightDensityLUT;
};
