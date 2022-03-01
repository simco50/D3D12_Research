#pragma once
#include "Graphics/RenderGraph/RenderGraphDefinitions.h"
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

	void OnResize(int windowWidth, int windowHeight);

	void Execute(RGGraph& graph, const SceneView& resources, const SceneTextures& parameters);
	void VisualizeLightDensity(RGGraph& graph, GraphicsDevice* pDevice, const SceneView& resources, Texture* pTarget, Texture* pDepth);

private:
	void SetupPipelines();

	GraphicsDevice* m_pDevice;

	//Light Culling
	RefCountPtr<RootSignature> m_pComputeLightCullRS;
	RefCountPtr<PipelineState> m_pComputeLightCullPSO;
	RefCountPtr<Buffer> m_pLightIndexCounter;
	RefCountPtr<UnorderedAccessView> m_pLightIndexCounterRawUAV;
	RefCountPtr<Buffer> m_pLightIndexListBufferOpaque;
	RefCountPtr<Texture> m_pLightGridOpaque;
	RefCountPtr<Buffer> m_pLightIndexListBufferTransparant;
	RefCountPtr<Texture> m_pLightGridTransparant;

	//Diffuse
	RefCountPtr<RootSignature> m_pDiffuseRS;
	RefCountPtr<PipelineState> m_pDiffusePSO;
	RefCountPtr<PipelineState> m_pDiffuseMaskedPSO;
	RefCountPtr<PipelineState> m_pDiffuseAlphaPSO;

	//Visualize Light Count
	RefCountPtr<RootSignature> m_pVisualizeLightsRS;
	RefCountPtr<PipelineState> m_pVisualizeLightsPSO;
	RefCountPtr<Texture> m_pVisualizationIntermediateTexture;
};
