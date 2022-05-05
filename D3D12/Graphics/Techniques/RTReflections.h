#pragma once
class GraphicsDevice;
class RootSignature;
class RGGraph;
struct SceneView;
struct SceneTextures;
class StateObject;

class RTReflections
{
public:
	RTReflections(GraphicsDevice* pDevice);
	void Execute(RGGraph& graph, const SceneView& view, SceneTextures& sceneTextures);

private:
	RefCountPtr<StateObject> m_pRtSO;
	RefCountPtr<RootSignature> m_pGlobalRS;
};

