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
class RTReflections
{
public:
	RTReflections(Graphics* pGraphics);

	void Execute(RGGraph& graph, const SceneData& sceneData);
	void OnResize(uint32 width, uint32 height);

private:
	void SetupResources(Graphics* pGraphics);
	void SetupPipelines(Graphics* pGraphics);

	ComPtr<ID3D12StateObject> m_pRtSO;

	std::unique_ptr<RootSignature> m_pHitSignature;
	std::unique_ptr<RootSignature> m_pGlobalRS;

	std::unique_ptr<Texture> m_pSceneColor;
};

