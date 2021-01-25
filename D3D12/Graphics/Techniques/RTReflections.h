#pragma once
class Mesh;
class Graphics;
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
	RTReflections(Graphics* pGraphics);

	void Execute(RGGraph& graph, const SceneData& sceneData);
	void OnResize(uint32 width, uint32 height);

private:
	void SetupResources(Graphics* pGraphics);
	void SetupPipelines(Graphics* pGraphics);

	StateObject* m_pRtSO = nullptr;

	std::unique_ptr<RootSignature> m_pHitSignature;
	std::unique_ptr<RootSignature> m_pGlobalRS;

	std::unique_ptr<Texture> m_pSceneColor;
};

