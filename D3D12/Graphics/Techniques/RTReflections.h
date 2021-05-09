#pragma once
class Mesh;
class GraphicsDevice;
class RootSignature;
class Texture;
class Camera;
class CommandContext;
class RGGraph;
class Buffer;
struct SceneData;
class StateObject;

class RTReflections
{
public:
	RTReflections(GraphicsDevice* pDevice);

	void Execute(RGGraph& graph, const SceneData& sceneData);
	void OnResize(uint32 width, uint32 height);

private:
	void SetupResources(GraphicsDevice* pDevice);
	void SetupPipelines(GraphicsDevice* pDevice);

	StateObject* m_pRtSO = nullptr;

	std::unique_ptr<RootSignature> m_pHitSignature;
	std::unique_ptr<RootSignature> m_pGlobalRS;

	std::unique_ptr<Texture> m_pSceneColor;
};

