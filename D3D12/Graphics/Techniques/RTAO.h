#pragma once
class Mesh;
class Graphics;
class RootSignature;
class Texture;
class Camera;
class CommandContext;
class RGGraph;
class Buffer;

class RTAO
{
public:
	RTAO(Graphics* pGraphics);

	void Execute(RGGraph& graph, Texture* pColor, Texture* pDepth, Buffer* pTLAS, Camera& camera);

private:
	void SetupResources(Graphics* pGraphics);
	void SetupPipelines(Graphics* pGraphics);

	ComPtr<ID3D12StateObject> m_pRtSO;
	std::unique_ptr<RootSignature> m_pGlobalRS;
};

