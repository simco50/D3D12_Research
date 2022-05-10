#pragma once

class RootSignature;
class GraphicsDevice;
class PipelineState;
class Texture;
class Buffer;
class UnorderedAccessView;
class RGGraph;
struct SceneView;
struct SceneTextures;

class TiledForward
{
public:
	TiledForward(GraphicsDevice* pDevice);

	void Execute(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures);
	void VisualizeLightDensity(RGGraph& graph, GraphicsDevice* pDevice, const SceneView* pView, SceneTextures& sceneTextures);

private:
	GraphicsDevice* m_pDevice;

	//Light Culling
	RefCountPtr<RootSignature> m_pComputeLightCullRS;
	RefCountPtr<PipelineState> m_pComputeLightCullPSO;

	//Diffuse
	RefCountPtr<RootSignature> m_pDiffuseRS;
	RefCountPtr<PipelineState> m_pDiffusePSO;
	RefCountPtr<PipelineState> m_pDiffuseMaskedPSO;
	RefCountPtr<PipelineState> m_pDiffuseAlphaPSO;

	//Visualize Light Count
	RefCountPtr<RootSignature> m_pVisualizeLightsRS;
	RefCountPtr<PipelineState> m_pVisualizeLightsPSO;
};
