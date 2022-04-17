#pragma once
class Mesh;
class GraphicsDevice;
class RootSignature;
class Texture;
class CommandContext;
class RGGraph;
class Buffer;
class StateObject;
class PipelineState;
struct SceneView;
struct SceneTextures;

class RTAO
{
public:
	RTAO(GraphicsDevice* pDevice);

	void Execute(RGGraph& graph, const SceneView& view, SceneTextures& sceneTextures);

private:
	void SetupPipelines(GraphicsDevice* pDevice);
	GraphicsDevice* m_pDevice = nullptr;

	RefCountPtr<Texture> m_Targets[2];
	RefCountPtr<Texture> m_pHistory;

	RefCountPtr<StateObject> m_pTraceRaysSO;
	RefCountPtr<RootSignature> m_pCommonRS;
	RefCountPtr<PipelineState> m_pDenoisePSO;
	RefCountPtr<PipelineState> m_pBilateralBlurPSO;
};
