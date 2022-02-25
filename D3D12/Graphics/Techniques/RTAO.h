#pragma once
class Mesh;
class GraphicsDevice;
class RootSignature;
class Texture;
class CommandContext;
class RGGraph;
class Buffer;
class StateObject;
struct SceneView;

class RTAO
{
public:
	RTAO(GraphicsDevice* pDevice);

	void Execute(RGGraph& graph, const SceneView& sceneData, Texture* pTarget, Texture* pDepth);

private:
	void SetupPipelines(GraphicsDevice* pDevice);

	StateObject* m_pRtSO = nullptr;
	std::unique_ptr<RootSignature> m_pGlobalRS;
};
