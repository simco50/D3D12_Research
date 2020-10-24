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

private:
	void SetupResources(Graphics* pGraphics);
	void SetupPipelines(Graphics* pGraphics);

	ComPtr<ID3D12StateObject> m_pRtSO;

	std::unique_ptr<RootSignature> m_pRayGenSignature;
	std::unique_ptr<RootSignature> m_pHitSignature;
	std::unique_ptr<RootSignature> m_pMissSignature;
	std::unique_ptr<RootSignature> m_pGlobalRS;

	std::unique_ptr<Texture> m_pTestOutput;
};

