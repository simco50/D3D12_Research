#pragma once

class Graphics;
class StructuredBuffer;
class ByteAddressBuffer;
class CommandSignature;
class ComputePipelineState;
class GraphicsPipelineState;
class RootSignature;

class GpuParticles
{
public:
	GpuParticles(Graphics* pGraphics);
	~GpuParticles();

	void Initialize();
	void Simulate();
	void Render();
private:
	Graphics* m_pGraphics;

	std::unique_ptr<StructuredBuffer> m_pAliveList1;
	std::unique_ptr<StructuredBuffer> m_pAliveList2;
	std::unique_ptr<StructuredBuffer> m_pDeadList;
	std::unique_ptr<StructuredBuffer> m_pParticleBuffer;
	std::unique_ptr<ByteAddressBuffer> m_pCountersBuffer;

	std::unique_ptr<RootSignature> m_pPrepareArgumentsRS;
	std::unique_ptr<ComputePipelineState> m_pPrepareArgumentsPS;

	std::unique_ptr<RootSignature> m_pEmitRS;
	std::unique_ptr<ComputePipelineState> m_pEmitPS;
	std::unique_ptr<ByteAddressBuffer> m_pEmitArguments;

	std::unique_ptr<RootSignature> m_pSimulateRS;
	std::unique_ptr<ComputePipelineState> m_pSimulatePS;
	std::unique_ptr<ByteAddressBuffer> m_pSimulateArguments;

	std::unique_ptr<RootSignature> m_pSimulateEndRS;
	std::unique_ptr<ComputePipelineState> m_pSimulateEndPS;
	std::unique_ptr<ByteAddressBuffer> m_pDrawArguments;

	std::unique_ptr<CommandSignature> m_pSimpleDispatchCommandSignature;
	std::unique_ptr<CommandSignature> m_pSimpleDrawCommandSignature;

	std::unique_ptr<RootSignature> m_pRenderParticlesRS;
	std::unique_ptr<GraphicsPipelineState> m_pRenderParticlesPS;
};

