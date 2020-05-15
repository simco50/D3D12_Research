#include "stdafx.h"
#include "GpuParticles.h"
#include "Graphics/Core/GraphicsBuffer.h"
#include "CommandSignature.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/Core/Shader.h"
#include "Graphics/Core/PipelineState.h"
#include "Graphics/Core/RootSignature.h"
#include "Graphics/Core/CommandContext.h"
#include "Profiler.h"
#include "Graphics/Core/GraphicsResource.h"
#include "Scene/Camera.h"
#include "Graphics/Core/Texture.h"
#include "Core/ResourceViews.h"

static constexpr uint32 cMaxParticleCount = 2000000;

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
	CommandContext* pContext = m_pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);

	m_pCountersBuffer = std::make_unique<Buffer>(m_pGraphics);
	m_pCountersBuffer->Create(BufferDesc::CreateByteAddress(sizeof(uint32) * 4));

	m_pAliveList1 = std::make_unique<Buffer>(m_pGraphics);
	m_pAliveList1->Create(BufferDesc::CreateStructured(cMaxParticleCount, sizeof(uint32)));
	m_pAliveList2 = std::make_unique<Buffer>(m_pGraphics);
	m_pAliveList2->Create(BufferDesc::CreateStructured(cMaxParticleCount, sizeof(uint32)));
	m_pDeadList = std::make_unique<Buffer>(m_pGraphics);
	m_pDeadList->Create(BufferDesc::CreateStructured(cMaxParticleCount, sizeof(uint32)));
	std::vector<uint32> deadList(cMaxParticleCount);
	std::generate(deadList.begin(), deadList.end(), [n = 0]() mutable { return n++; });
	m_pDeadList->SetData(pContext, deadList.data(), sizeof(uint32) * deadList.size());
	uint32 aliveCount = cMaxParticleCount;
	m_pCountersBuffer->SetData(pContext, &aliveCount, sizeof(uint32), 0);

	m_pParticleBuffer = std::make_unique<Buffer>(m_pGraphics);
	m_pParticleBuffer->Create(BufferDesc::CreateStructured(cMaxParticleCount, sizeof(ParticleData)));

	m_pEmitArguments = std::make_unique<Buffer>(m_pGraphics);
	m_pEmitArguments->Create(BufferDesc::CreateIndirectArguments<uint32>(3));
	m_pSimulateArguments = std::make_unique<Buffer>(m_pGraphics);
	m_pSimulateArguments->Create(BufferDesc::CreateIndirectArguments<uint32>(3));
	m_pDrawArguments = std::make_unique<Buffer>(m_pGraphics);
	m_pDrawArguments->Create(BufferDesc::CreateIndirectArguments<uint32>(4));

	pContext->Execute(true);

	m_pSimpleDispatchCommandSignature = std::make_unique<CommandSignature>();
	m_pSimpleDispatchCommandSignature->AddDispatch();
	m_pSimpleDispatchCommandSignature->Finalize("Simple Dispatch", m_pGraphics->GetDevice());

	m_pSimpleDrawCommandSignature = std::make_unique<CommandSignature>();
	m_pSimpleDrawCommandSignature->AddDraw();
	m_pSimpleDrawCommandSignature->Finalize("Simple Draw", m_pGraphics->GetDevice());

	{
		Shader computeShader("Resources/Shaders/ParticleSimulation.hlsl", Shader::Type::Compute, "UpdateSimulationParameters", { "COMPILE_UPDATE_PARAMETERS" });
		m_pPrepareArgumentsRS = std::make_unique<RootSignature>();
		m_pPrepareArgumentsRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
		m_pPrepareArgumentsRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 3, D3D12_SHADER_VISIBILITY_ALL);
		m_pPrepareArgumentsRS->Finalize("Prepare Particle Arguments RS", m_pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_NONE);
		m_pPrepareArgumentsPS = std::make_unique<PipelineState>();
		m_pPrepareArgumentsPS->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pPrepareArgumentsPS->SetRootSignature(m_pPrepareArgumentsRS->GetRootSignature());
		m_pPrepareArgumentsPS->Finalize("Prepare Particle Arguments PS", m_pGraphics->GetDevice());
	}
	{
		Shader computeShader("Resources/Shaders/ParticleSimulation.hlsl", Shader::Type::Compute, "Emit", { "COMPILE_EMITTER" });
		m_pEmitRS = std::make_unique<RootSignature>();
		m_pEmitRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
		m_pEmitRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 4, D3D12_SHADER_VISIBILITY_ALL);
		m_pEmitRS->Finalize("Particle Emitter RS", m_pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_NONE);
		m_pEmitPS = std::make_unique<PipelineState>();
		m_pEmitPS->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pEmitPS->SetRootSignature(m_pEmitRS->GetRootSignature());
		m_pEmitPS->Finalize("Particle Emitter PS", m_pGraphics->GetDevice());
	}
	{
		Shader computeShader("Resources/Shaders/ParticleSimulation.hlsl", Shader::Type::Compute, "Simulate", { "COMPILE_SIMULATE" });
		m_pSimulateRS = std::make_unique<RootSignature>();
		m_pSimulateRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
		m_pSimulateRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 5, D3D12_SHADER_VISIBILITY_ALL);
		m_pSimulateRS->Finalize("Particle Simulation RS", m_pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_NONE);
		m_pSimulatePS = std::make_unique<PipelineState>();
		m_pSimulatePS->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pSimulatePS->SetRootSignature(m_pSimulateRS->GetRootSignature());
		m_pSimulatePS->Finalize("Particle Simulation PS", m_pGraphics->GetDevice());
	}
	{
		Shader computeShader("Resources/Shaders/ParticleSimulation.hlsl", Shader::Type::Compute, "SimulateEnd", { "COMPILE_SIMULATE_END" });
		m_pSimulateEndRS = std::make_unique<RootSignature>();
		m_pSimulateEndRS->SetDescriptorTableSimple(0, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, D3D12_SHADER_VISIBILITY_ALL);
		m_pSimulateEndRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, D3D12_SHADER_VISIBILITY_ALL);
		m_pSimulateEndRS->Finalize("Particle Simulation End RS", m_pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_NONE);
		m_pSimulateEndPS = std::make_unique<PipelineState>();
		m_pSimulateEndPS->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pSimulateEndPS->SetRootSignature(m_pSimulateEndRS->GetRootSignature());
		m_pSimulateEndPS->Finalize("Particle Simulation End PS", m_pGraphics->GetDevice());
	}
	{
		Shader vertexShader("Resources/Shaders/ParticleRendering.hlsl", Shader::Type::Vertex, "VSMain");
		Shader pixelShader("Resources/Shaders/ParticleRendering.hlsl", Shader::Type::Pixel, "PSMain");

		m_pRenderParticlesRS = std::make_unique<RootSignature>();
		m_pRenderParticlesRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
		m_pRenderParticlesRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, D3D12_SHADER_VISIBILITY_VERTEX);
		m_pRenderParticlesRS->Finalize("Particle Rendering", m_pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		m_pRenderParticlesPS = std::make_unique<PipelineState>();
		m_pRenderParticlesPS->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
		m_pRenderParticlesPS->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
		m_pRenderParticlesPS->SetRootSignature(m_pRenderParticlesRS->GetRootSignature());
		m_pRenderParticlesPS->SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
		m_pRenderParticlesPS->SetInputLayout(nullptr, 0);
		m_pRenderParticlesPS->SetDepthWrite(false);
		m_pRenderParticlesPS->SetCullMode(D3D12_CULL_MODE_NONE);
		m_pRenderParticlesPS->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		m_pRenderParticlesPS->SetRenderTargetFormat(Graphics::RENDER_TARGET_FORMAT, Graphics::DEPTH_STENCIL_FORMAT, m_pGraphics->GetMultiSampleCount(), m_pGraphics->GetMultiSampleQualityLevel(m_pGraphics->GetMultiSampleCount()));
		m_pRenderParticlesPS->Finalize("Particle Rendering PS", m_pGraphics->GetDevice());
	}
}

void GpuParticles::Simulate(CommandContext& context)
{
	{
		GPU_PROFILE_SCOPE("Prepare Arguments", &context);

		context.SetPipelineState(m_pPrepareArgumentsPS.get());
		context.SetComputeRootSignature(m_pPrepareArgumentsRS.get());

		context.InsertResourceBarrier(m_pEmitArguments.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		context.InsertResourceBarrier(m_pSimulateArguments.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		context.InsertResourceBarrier(m_pCountersBuffer.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		context.InsertResourceBarrier(m_pAliveList2.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		context.InsertResourceBarrier(m_pParticleBuffer.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		context.FlushResourceBarriers();

		struct Parameters
		{
			uint32 EmitCount;
		} parameters;
		parameters.EmitCount = 1000;

		D3D12_CPU_DESCRIPTOR_HANDLE uavs[] = {
			m_pCountersBuffer->GetUAV()->GetDescriptor(),
			m_pEmitArguments->GetUAV()->GetDescriptor(),
			m_pSimulateArguments->GetUAV()->GetDescriptor(),
		};
		context.SetComputeDynamicConstantBufferView(0, &parameters, sizeof(Parameters));
		context.SetDynamicDescriptors(1, 0, uavs, 3);

		context.Dispatch(1, 1, 1);
		context.InsertUavBarrier();
		context.InsertResourceBarrier(m_pEmitArguments.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
		context.InsertResourceBarrier(m_pSimulateArguments.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
		context.FlushResourceBarriers();
	}
	{
		GPU_PROFILE_SCOPE("Emit", &context);

		context.SetPipelineState(m_pEmitPS.get());
		context.SetComputeRootSignature(m_pEmitRS.get());

		D3D12_CPU_DESCRIPTOR_HANDLE uavs[] = {
			m_pCountersBuffer->GetUAV()->GetDescriptor(),
			m_pDeadList->GetUAV()->GetDescriptor(),
			m_pAliveList1->GetUAV()->GetDescriptor(),
			m_pParticleBuffer->GetUAV()->GetDescriptor(),
		};
		context.SetDynamicDescriptors(1, 0, uavs, 4);

		std::array<Vector4, 64> randomDirections;
		std::generate(randomDirections.begin(), randomDirections.end(), []() { Vector3 v = Math::RandVector(); v.Normalize(); return Vector4(v.x, v.y, v.z, 0); });

		context.SetComputeDynamicConstantBufferView(0, randomDirections.data(), sizeof(Vector4) * randomDirections.size());

		context.ExecuteIndirect(m_pSimpleDispatchCommandSignature->GetCommandSignature(), m_pEmitArguments.get());

		context.InsertUavBarrier();
	}
	{
		GPU_PROFILE_SCOPE("Simulate", &context);

		context.SetPipelineState(m_pSimulatePS.get());
		context.SetComputeRootSignature(m_pSimulateRS.get());

		struct Parameters
		{
			float DeltaTime;
			float ParticleLifeTime;
		} parameters;
		parameters.DeltaTime = GameTimer::DeltaTime();
		parameters.ParticleLifeTime = 4.0f;

		context.SetComputeDynamicConstantBufferView(0, &parameters, sizeof(Parameters));

		D3D12_CPU_DESCRIPTOR_HANDLE uavs[] = {
			m_pCountersBuffer->GetUAV()->GetDescriptor(),
			m_pDeadList->GetUAV()->GetDescriptor(),
			m_pAliveList1->GetUAV()->GetDescriptor(),
			m_pAliveList2->GetUAV()->GetDescriptor(),
			m_pParticleBuffer->GetUAV()->GetDescriptor(),
		};
		context.SetDynamicDescriptors(1, 0, uavs, 5);

		context.ExecuteIndirect(m_pSimpleDispatchCommandSignature->GetCommandSignature(), m_pSimulateArguments.get());

		context.InsertUavBarrier();
	}
	{
		GPU_PROFILE_SCOPE("Simulate End", &context);

		context.InsertResourceBarrier(m_pDrawArguments.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		context.InsertResourceBarrier(m_pCountersBuffer.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

		context.SetPipelineState(m_pSimulateEndPS.get());
		context.SetComputeRootSignature(m_pSimulateEndRS.get());

		context.SetDynamicDescriptor(0, 0, m_pCountersBuffer->GetSRV());
		context.SetDynamicDescriptor(1, 0, m_pDrawArguments->GetUAV());

		context.Dispatch(1, 1, 1);

		context.InsertUavBarrier();
	}
	{
		GPU_PROFILE_SCOPE("Draw Particles", &context);

		context.InsertResourceBarrier(m_pDrawArguments.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
		context.InsertResourceBarrier(m_pAliveList2.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		context.InsertResourceBarrier(m_pParticleBuffer.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		context.InsertResourceBarrier(m_pGraphics->GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_RENDER_TARGET);

		context.BeginRenderPass(RenderPassInfo(m_pGraphics->GetCurrentRenderTarget(), RenderPassAccess::Load_Store, m_pGraphics->GetDepthStencil(), RenderPassAccess::Load_Store));

		context.SetPipelineState(m_pRenderParticlesPS.get());
		context.SetGraphicsRootSignature(m_pRenderParticlesRS.get());

		Vector2 screenDimensions((float)m_pGraphics->GetWindowWidth(), (float)m_pGraphics->GetWindowHeight());
		context.SetViewport(FloatRect(0, 0, (float)screenDimensions.x, (float)screenDimensions.y));
		context.SetScissorRect(FloatRect(0, 0, (float)screenDimensions.x, (float)screenDimensions.y));

		struct FrameData
		{
			Matrix ViewInverse;
			Matrix View;
			Matrix Projection;
		} frameData;
		frameData.ViewInverse = m_pGraphics->GetCamera()->GetViewInverse();
		frameData.View = m_pGraphics->GetCamera()->GetView();
		frameData.Projection = m_pGraphics->GetCamera()->GetProjection();

		context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context.SetDynamicConstantBufferView(0, &frameData, sizeof(FrameData));

		D3D12_CPU_DESCRIPTOR_HANDLE srvs[] = {
			m_pParticleBuffer->GetSRV()->GetDescriptor(),
			m_pAliveList2->GetSRV()->GetDescriptor()
		};
		context.SetDynamicDescriptors(1, 0, srvs, 2);

		context.ExecuteIndirect(m_pSimpleDrawCommandSignature->GetCommandSignature(), m_pDrawArguments.get(), false);

		context.EndRenderPass();
	}

	std::swap(m_pAliveList1, m_pAliveList2);
}

void GpuParticles::Render(CommandContext& context)
{

}
