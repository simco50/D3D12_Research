#pragma once

class Graphics;
class Buffer;
class CommandSignature;
class PipelineState;
class RootSignature;
class CommandContext;
class Texture;
class Camera;
class RGGraph;

class GpuParticles
{
public:
	GpuParticles(Graphics* pGraphics);
	~GpuParticles();

	void Simulate(RGGraph& graph, Texture* pSourceDepth, const Camera& camera);
	void Render(RGGraph& graph, Texture* pTarget, Texture* pDepth, const Camera& camera);
private:
	void Initialize(Graphics* pGraphics);

	std::unique_ptr<Buffer> m_pAliveList1;
	std::unique_ptr<Buffer> m_pAliveList2;
	std::unique_ptr<Buffer> m_pDeadList;
	std::unique_ptr<Buffer> m_pParticleBuffer;
	std::unique_ptr<Buffer> m_pCountersBuffer;

	PipelineState* m_pPrepareArgumentsPS = nullptr;

	PipelineState* m_pEmitPS = nullptr;
	std::unique_ptr<Buffer> m_pEmitArguments;

	std::unique_ptr<RootSignature> m_pSimulateRS;
	PipelineState* m_pSimulatePS = nullptr;
	std::unique_ptr<Buffer> m_pSimulateArguments;

	PipelineState* m_pSimulateEndPS = nullptr;
	std::unique_ptr<Buffer> m_pDrawArguments;

	std::unique_ptr<CommandSignature> m_pSimpleDispatchCommandSignature;
	std::unique_ptr<CommandSignature> m_pSimpleDrawCommandSignature;

	std::unique_ptr<RootSignature> m_pRenderParticlesRS;
	PipelineState* m_pRenderParticlesPS = nullptr;

	float m_ParticlesToSpawn = 0;
};

