#pragma once

class Graphics;
class Buffer;
class CommandSignature;
class PipelineState;
class RootSignature;
class CommandContext;
class Texture;
class Camera;

class GpuParticles
{
public:
	GpuParticles(Graphics* pGraphics);
	~GpuParticles();

	void Simulate(CommandContext& context, Texture* pSourceDepth, const Camera& camera);
	void Render(CommandContext& context, Texture* pTarget, Texture* pDepth, const Camera& camera);
private:
	void Initialize(Graphics* pGraphics);

	std::unique_ptr<Buffer> m_pAliveList1;
	std::unique_ptr<Buffer> m_pAliveList2;
	std::unique_ptr<Buffer> m_pDeadList;
	std::unique_ptr<Buffer> m_pParticleBuffer;
	std::unique_ptr<Buffer> m_pCountersBuffer;

	std::unique_ptr<PipelineState> m_pPrepareArgumentsPS;

	std::unique_ptr<PipelineState> m_pEmitPS;
	std::unique_ptr<Buffer> m_pEmitArguments;

	std::unique_ptr<RootSignature> m_pSimulateRS;
	std::unique_ptr<PipelineState> m_pSimulatePS;
	std::unique_ptr<Buffer> m_pSimulateArguments;

	std::unique_ptr<PipelineState> m_pSimulateEndPS;
	std::unique_ptr<Buffer> m_pDrawArguments;

	std::unique_ptr<CommandSignature> m_pSimpleDispatchCommandSignature;
	std::unique_ptr<CommandSignature> m_pSimpleDrawCommandSignature;

	std::unique_ptr<RootSignature> m_pRenderParticlesRS;
	std::unique_ptr<PipelineState> m_pRenderParticlesPS;

	float m_ParticlesToSpawn = 0;
};

