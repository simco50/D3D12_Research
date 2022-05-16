#pragma once

class PipelineState;
class RootSignature;
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
	void Render(RGGraph& graph, SceneTextures& sceneTextures, const SceneView* pView);
	Texture* GetNoiseTexture() const { return m_pWorleyNoiseTexture; }

private:
	RefCountPtr<PipelineState> m_pWorleyNoisePS;
	RefCountPtr<RootSignature> m_pWorleyNoiseRS;
	RefCountPtr<Texture> m_pWorleyNoiseTexture;

	RefCountPtr<PipelineState> m_pCloudsPS;
	RefCountPtr<RootSignature> m_pCloudsRS;

	RefCountPtr<Buffer> m_pQuadVertexBuffer;

	bool m_UpdateNoise = true;
	BoundingBox m_CloudBounds;
	RefCountPtr<Texture> m_pVerticalDensityTexture;
};
