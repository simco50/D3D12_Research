#pragma once
class Mesh;
class GraphicsDevice;
class RootSignature;
class Texture;
class Camera;
class CommandContext;
class RGGraph;
class Buffer;
class StateObject;
struct SceneData;

class RTAO
{
public:
	RTAO(GraphicsDevice* pDevice);

	void Execute(RGGraph& graph, Texture* pColor, Texture* pDepth, const SceneData& sceneData, Camera& camera);

private:
	void SetupResources(GraphicsDevice* pDevice);
	void SetupPipelines(GraphicsDevice* pDevice);

	StateObject* m_pRtSO = nullptr;
	std::unique_ptr<RootSignature> m_pGlobalRS;
};

