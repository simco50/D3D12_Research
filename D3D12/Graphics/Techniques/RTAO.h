#pragma once
class GraphicsDevice;
class RootSignature;
class Texture;
class RGGraph;
class StateObject;
class PipelineState;
struct SceneView;
struct SceneTextures;

class RTAO
{
public:
	RTAO(GraphicsDevice* pDevice);

	void Execute(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures);

private:
	RefCountPtr<Texture> m_pHistory;

	RefCountPtr<StateObject> m_pTraceRaysSO;
	RefCountPtr<RootSignature> m_pCommonRS;
	RefCountPtr<PipelineState> m_pDenoisePSO;
	RefCountPtr<PipelineState> m_pBilateralBlurPSO;
};
