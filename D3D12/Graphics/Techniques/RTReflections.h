#pragma once
class Mesh;
class GraphicsDevice;
class RootSignature;
class Texture;
class CommandContext;
class RGGraph;
class Buffer;
struct SceneView;
class StateObject;

class RTReflections
{
public:
	RTReflections(GraphicsDevice* pDevice);

	void Execute(RGGraph& graph, const SceneView& sceneData, Texture* pColorTarget, Texture* pNormals, Texture* pDepth);
	void OnResize(uint32 width, uint32 height);

private:
	void SetupPipelines(GraphicsDevice* pDevice);

	GraphicsDevice* m_pDevice;

	StateObject* m_pRtSO = nullptr;
	std::unique_ptr<RootSignature> m_pGlobalRS;
	std::unique_ptr<Texture> m_pSceneColor;
};

