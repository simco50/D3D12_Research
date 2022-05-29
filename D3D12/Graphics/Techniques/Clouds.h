#pragma once
#include "../RenderGraph/RenderGraphDefinitions.h"

class Texture;
class GraphicsDevice;
class Buffer;
class RGGraph;
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
	RefCountPtr<Buffer> m_pQuadVertexBuffer;
};
