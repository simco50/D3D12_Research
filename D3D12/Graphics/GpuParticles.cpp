#include "stdafx.h"
#include "GpuParticles.h"
#include "GraphicsBuffer.h"
#include "CommandSignature.h"
#include "Graphics.h"
#include "Shader.h"
#include "PipelineState.h"
#include "RootSignature.h"
#include "CommandContext.h"
#include "Profiler.h"
#include "GraphicsResource.h"

static constexpr uint32 cMaxParticleCount = 2048;

struct ParticleData
{
	Vector3 Position;
	float LifeTime;
	Vector3 Velocity;
};

GpuParticles::GpuParticles(Graphics* pGraphics)
	: m_pGraphics(pGraphics)
{
}

GpuParticles::~GpuParticles()
{
}

void GpuParticles::Initialize()
{
	GraphicsCommandContext* pContext = static_cast<GraphicsCommandContext*>(m_pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT));

	m_pCountersBuffer = std::make_unique<ByteAddressBuffer>(m_pGraphics);
	m_pCountersBuffer->Create(m_pGraphics, sizeof(uint32), 4);

	m_pAliveList1 = std::make_unique<StructuredBuffer>(m_pGraphics);
	m_pAliveList1->Create(m_pGraphics, sizeof(uint32), cMaxParticleCount);
	m_pAliveList2 = std::make_unique<StructuredBuffer>(m_pGraphics);
	m_pAliveList2->Create(m_pGraphics, sizeof(uint32), cMaxParticleCount);
	m_pDeadList = std::make_unique<StructuredBuffer>(m_pGraphics);
	m_pDeadList->Create(m_pGraphics, sizeof(uint32), cMaxParticleCount);
	std::array<uint32, cMaxParticleCount> deadList;
	std::generate(deadList.begin(), deadList.end(), [n = 0]() mutable { return n++; });
	m_pDeadList->SetData(pContext, deadList.data(), sizeof(uint32) * deadList.size());
	uint32 aliveCount = cMaxParticleCount;
	m_pCountersBuffer->SetData(pContext, &aliveCount, sizeof(uint32), 0);

	m_pParticleBuffer = std::make_unique<StructuredBuffer>(m_pGraphics);
	m_pParticleBuffer->Create(m_pGraphics, sizeof(ParticleData), cMaxParticleCount);

	m_pEmitArguments = std::make_unique<ByteAddressBuffer>(m_pGraphics);
	m_pEmitArguments->Create(m_pGraphics, sizeof(uint32), 3);
	m_pSimulateArguments = std::make_unique<ByteAddressBuffer>(m_pGraphics);
	m_pSimulateArguments->Create(m_pGraphics, sizeof(uint32), 3);

	pContext->Execute(true);

	m_pSimpleDispatchCommandSignature = std::make_unique<CommandSignature>();
	m_pSimpleDispatchCommandSignature->AddDispatch();
	m_pSimpleDispatchCommandSignature->Finalize("Simple Dispatch", m_pGraphics->GetDevice());

	{
		Shader computeShader("Resources/Shaders/ParticleSimulation.hlsl", Shader::Type::ComputeShader, "UpdateSimulationParameters", { "COMPILE_UPDATE_PARAMETERS" });
		m_pPrepareArgumentsRS = std::make_unique<RootSignature>();
		m_pPrepareArgumentsRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
		m_pPrepareArgumentsRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 3, D3D12_SHADER_VISIBILITY_ALL);
		m_pPrepareArgumentsRS->Finalize("Prepare Particle Arguments RS", m_pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_NONE);
		m_pPrepareArgumentsPS = std::make_unique<ComputePipelineState>();
		m_pPrepareArgumentsPS->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pPrepareArgumentsPS->SetRootSignature(m_pPrepareArgumentsRS->GetRootSignature());
		m_pPrepareArgumentsPS->Finalize("Prepare Particle Arguments PS", m_pGraphics->GetDevice());
	}
	{
		Shader computeShader("Resources/Shaders/ParticleSimulation.hlsl", Shader::Type::ComputeShader, "Emit", { "COMPILE_EMITTER" });
		m_pEmitRS = std::make_unique<RootSignature>();
		m_pEmitRS->SetDescriptorTableSimple(0, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 4, D3D12_SHADER_VISIBILITY_ALL);
		m_pEmitRS->Finalize("Particle Emitter RS", m_pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_NONE);
		m_pEmitPS = std::make_unique<ComputePipelineState>();
		m_pEmitPS->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pEmitPS->SetRootSignature(m_pEmitRS->GetRootSignature());
		m_pEmitPS->Finalize("Particle Emitter PS", m_pGraphics->GetDevice());
	}
	{
		Shader computeShader("Resources/Shaders/ParticleSimulation.hlsl", Shader::Type::ComputeShader, "Simulate", { "COMPILE_SIMULATE" });
		m_pSimulateRS = std::make_unique<RootSignature>();
		m_pSimulateRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
		m_pSimulateRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 5, D3D12_SHADER_VISIBILITY_ALL);
		m_pSimulateRS->Finalize("Particle SimulationRS", m_pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_NONE);
		m_pSimulatePS = std::make_unique<ComputePipelineState>();
		m_pSimulatePS->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pSimulatePS->SetRootSignature(m_pSimulateRS->GetRootSignature());
		m_pSimulatePS->Finalize("Particle Simulation PS", m_pGraphics->GetDevice());
	}
	{
		Shader vertexShader("Resources/Shaders/ParticleRendering.hlsl", Shader::Type::VertexShader, "VSMain");
		Shader geometryShader("Resources/Shaders/ParticleRendering.hlsl", Shader::Type::GeometryShader, "GSMain");
		Shader pixelShader("Resources/Shaders/ParticleRendering.hlsl", Shader::Type::PixelShader, "PSMain");

		m_pRenderParticlesRS = std::make_unique<RootSignature>();
		m_pRenderParticlesRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_GEOMETRY);
		m_pRenderParticlesRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, D3D12_SHADER_VISIBILITY_GEOMETRY);
		m_pRenderParticlesRS->Finalize("Particle Rendering", m_pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		m_pRenderParticlesPS = std::make_unique<GraphicsPipelineState>();
		m_pRenderParticlesPS->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
		m_pRenderParticlesPS->SetGeometryShader(geometryShader.GetByteCode(), geometryShader.GetByteCodeSize());
		m_pRenderParticlesPS->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
		m_pRenderParticlesPS->SetRootSignature(m_pRenderParticlesRS->GetRootSignature());
		m_pRenderParticlesPS->SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT);
		m_pRenderParticlesPS->SetInputLayout(nullptr, 0);
		m_pRenderParticlesPS->SetRenderTargetFormat(Graphics::RENDER_TARGET_FORMAT, Graphics::DEPTH_STENCIL_FORMAT, m_pGraphics->GetMultiSampleCount(), m_pGraphics->GetMultiSampleQualityLevel(m_pGraphics->GetMultiSampleCount()));
		m_pRenderParticlesPS->Finalize("Particle Rendering PS", m_pGraphics->GetDevice());
	}
}

void GpuParticles::Simulate()
{
	GraphicsCommandContext* pContext = static_cast<GraphicsCommandContext*>(m_pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT));
	{
		Profiler::Instance()->Begin("Prepare Arguments", pContext);
		pContext->SetComputePipelineState(m_pPrepareArgumentsPS.get());
		pContext->SetComputeRootSignature(m_pPrepareArgumentsRS.get());

		pContext->InsertResourceBarrier(m_pEmitArguments.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		pContext->InsertResourceBarrier(m_pSimulateArguments.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		pContext->FlushResourceBarriers();

		struct Parameters
		{
			uint32 EmitCount;
		} parameters;
		parameters.EmitCount = 5;

		D3D12_CPU_DESCRIPTOR_HANDLE uavs[] = {
			m_pCountersBuffer->GetUAV(),
			m_pEmitArguments->GetUAV(),
			m_pSimulateArguments->GetUAV(),
		};
		pContext->SetComputeDynamicConstantBufferView(0, &parameters, sizeof(Parameters));
		pContext->SetDynamicDescriptors(1, 0, uavs, 3);

		pContext->Dispatch(1, 1, 1);
		pContext->InsertUavBarrier();
		pContext->InsertResourceBarrier(m_pEmitArguments.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
		pContext->InsertResourceBarrier(m_pSimulateArguments.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
		pContext->FlushResourceBarriers();

		Profiler::Instance()->End(pContext);
	}
	{
		Profiler::Instance()->Begin("Emit", pContext);
		pContext->SetComputePipelineState(m_pEmitPS.get());
		pContext->SetComputeRootSignature(m_pEmitRS.get());

		D3D12_CPU_DESCRIPTOR_HANDLE uavs[] = {
			m_pCountersBuffer->GetUAV(),
			m_pDeadList->GetUAV(),
			m_pAliveList1->GetUAV(),
			m_pParticleBuffer->GetUAV(),
		};
		pContext->SetDynamicDescriptors(0, 0, uavs, 4);

		pContext->ExecuteIndirect(m_pSimpleDispatchCommandSignature->GetCommandSignature(), m_pEmitArguments.get());

		pContext->InsertUavBarrier();

		Profiler::Instance()->End(pContext);
	}
	{
		Profiler::Instance()->Begin("Simulate", pContext);
		pContext->SetComputePipelineState(m_pSimulatePS.get());
		pContext->SetComputeRootSignature(m_pSimulateRS.get());

		struct Parameters
		{
			float DeltaTime;
			float ParticleLifeTime;
		} parameters;
		parameters.DeltaTime = GameTimer::DeltaTime();
		parameters.ParticleLifeTime = 2.0f;

		pContext->SetComputeDynamicConstantBufferView(0, &parameters, sizeof(Parameters));

		D3D12_CPU_DESCRIPTOR_HANDLE uavs[] = {
			m_pCountersBuffer->GetUAV(),
			m_pDeadList->GetUAV(),
			m_pAliveList1->GetUAV(),
			m_pAliveList2->GetUAV(),
			m_pParticleBuffer->GetUAV(),
		};
		pContext->SetDynamicDescriptors(1, 0, uavs, 5);

		pContext->ExecuteIndirect(m_pSimpleDispatchCommandSignature->GetCommandSignature(), m_pSimulateArguments.get());

		pContext->InsertUavBarrier();
		Profiler::Instance()->End(pContext);
	}
	{
	
	}

	std::swap(m_pAliveList1, m_pAliveList2);

	pContext->Execute(true);
}

void GpuParticles::Render()
{

}
