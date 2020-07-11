#pragma once
class Mesh;
class Graphics;
class RootSignature;
class Texture;
class Camera;
class CommandContext;
class Buffer;
class RGGraph;

class RTAO
{
public:
	RTAO(Graphics* pGraphics);

	void OnSwapchainCreated(int windowWidth, int windowHeight);

	void Execute(RGGraph& graph, Texture* pColor, Texture* pDepth, Camera& camera);
	void GenerateAccelerationStructure(Graphics* pGraphics, Mesh* pMesh, CommandContext& context);

private:
	void SetupResources(Graphics* pGraphics);
	void SetupPipelines(Graphics* pGraphics);

	std::unique_ptr<Buffer> m_pBLAS;
	std::unique_ptr<Buffer> m_pTLAS;
	std::unique_ptr<Buffer> m_pBLASScratch;
	std::unique_ptr<Buffer> m_pTLASScratch;
	std::unique_ptr<Buffer> m_pDescriptorsBuffer;

	ComPtr<ID3D12StateObject> m_pRtSO;

	std::unique_ptr<RootSignature> m_pRayGenSignature;
	std::unique_ptr<RootSignature> m_pHitSignature;
	std::unique_ptr<RootSignature> m_pMissSignature;
	std::unique_ptr<RootSignature> m_pGlobalRS;
};

