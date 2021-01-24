#pragma once
class Mesh;
class Graphics;
class RootSignature;
class Texture;
class Camera;
class CommandContext;
class RGGraph;
class Buffer;
class StateObject;

class RTAO
{
public:
	RTAO(Graphics* pGraphics);

	void Execute(RGGraph& graph, Texture* pColor, Texture* pDepth, Buffer* pTLAS, Camera& camera);

private:
	void SetupResources(Graphics* pGraphics);
	void SetupPipelines(Graphics* pGraphics);

	StateObject* m_pRtSO = nullptr;
	std::unique_ptr<RootSignature> m_pGlobalRS;
};

