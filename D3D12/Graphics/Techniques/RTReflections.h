#pragma once
class Mesh;
class GraphicsDevice;
class RootSignature;
class Texture;
class CommandContext;
class RGGraph;
class Buffer;
struct SceneView;
struct SceneTextures;
class StateObject;

class RTReflections
{
public:
	RTReflections(GraphicsDevice* pDevice);

	void Execute(RGGraph& graph, const SceneView& sceneData, const SceneTextures& sceneTextures);
	void OnResize(uint32 width, uint32 height);

private:
	void SetupPipelines(GraphicsDevice* pDevice);

	GraphicsDevice* m_pDevice;

	RefCountPtr<StateObject> m_pRtSO;
	RefCountPtr<RootSignature> m_pGlobalRS;
	RefCountPtr<Texture> m_pSceneColor;
};

