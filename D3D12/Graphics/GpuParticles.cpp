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
#include "ImGuiRenderer.h"

static int32 g_EmitCount = 30;
static float g_LifeTime = 4.0f;
static bool g_Simulate = true;

static constexpr uint32 cMaxParticleCount = 2000000;

struct ParticleData
{
	Vector3 Position;
	float LifeTime;
	Vector3 Velocity;
	float Size;
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

	BufferDesc particleBufferDesc = BufferDesc::CreateStructured(cMaxParticleCount, sizeof(uint32));
	m_pAliveList1 = std::make_unique<Buffer>(m_pGraphics);
	m_pAliveList1->Create(particleBufferDesc);
	m_pAliveList2 = std::make_unique<Buffer>(m_pGraphics);
	m_pAliveList2->Create(particleBufferDesc);
	m_pDeadList = std::make_unique<Buffer>(m_pGraphics);
	m_pDeadList->Create(particleBufferDesc);
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
		Shader computeShader("Resources/Shaders/ParticleSimulation.hlsl", Shader::Type::Compute, "UpdateSimulationParameters");
		m_pSimulateRS = std::make_unique<RootSignature>();
		m_pSimulateRS->FinalizeFromShader("Particle Simulation RS", computeShader, m_pGraphics->GetDevice());
	}

	{
		Shader computeShader("Resources/Shaders/ParticleSimulation.hlsl", Shader::Type::Compute, "UpdateSimulationParameters");
		m_pPrepareArgumentsPS = std::make_unique<PipelineState>();
		m_pPrepareArgumentsPS->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pPrepareArgumentsPS->SetRootSignature(m_pSimulateRS->GetRootSignature());
		m_pPrepareArgumentsPS->Finalize("Prepare Particle Arguments PS", m_pGraphics->GetDevice());
	}
	{
		Shader computeShader("Resources/Shaders/ParticleSimulation.hlsl", Shader::Type::Compute, "Emit");
		m_pEmitPS = std::make_unique<PipelineState>();
		m_pEmitPS->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pEmitPS->SetRootSignature(m_pSimulateRS->GetRootSignature());
		m_pEmitPS->Finalize("Particle Emitter PS", m_pGraphics->GetDevice());
	}
	{
		Shader computeShader("Resources/Shaders/ParticleSimulation.hlsl", Shader::Type::Compute, "Simulate");
		m_pSimulatePS = std::make_unique<PipelineState>();
		m_pSimulatePS->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pSimulatePS->SetRootSignature(m_pSimulateRS->GetRootSignature());
		m_pSimulatePS->Finalize("Particle Simulation PS", m_pGraphics->GetDevice());
	}
	{
		Shader computeShader("Resources/Shaders/ParticleSimulation.hlsl", Shader::Type::Compute, "SimulateEnd");
		m_pSimulateEndPS = std::make_unique<PipelineState>();
		m_pSimulateEndPS->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pSimulateEndPS->SetRootSignature(m_pSimulateRS->GetRootSignature());
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
		m_pRenderParticlesPS->SetBlendMode(BlendMode::Alpha, false);
		m_pRenderParticlesPS->SetCullMode(D3D12_CULL_MODE_NONE);
		m_pRenderParticlesPS->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		m_pRenderParticlesPS->SetRenderTargetFormat(Graphics::RENDER_TARGET_FORMAT, Graphics::DEPTH_STENCIL_FORMAT, m_pGraphics->GetMultiSampleCount(), m_pGraphics->GetMultiSampleQualityLevel(m_pGraphics->GetMultiSampleCount()));
		m_pRenderParticlesPS->Finalize("Particle Rendering PS", m_pGraphics->GetDevice());
	}

	m_pGraphics->GetImGui()->AddUpdateCallback(ImGuiCallbackDelegate::CreateLambda([]() {
		ImGui::Begin("Parameters");
		ImGui::Text("Particles");
		ImGui::Checkbox("Simulate", &g_Simulate);
		ImGui::SliderInt("Emit Count", &g_EmitCount, 0, 50);
		ImGui::SliderFloat("Life Time", &g_LifeTime, 0, 10);
		ImGui::End();
		}));
}

void GpuParticles::Simulate(CommandContext& context, Texture* pResolvedDepth, Texture* pNormals)
{
	if (!g_Simulate)
	{
		return;
	}

	static float time = 0;
	time += GameTimer::DeltaTime();
	if (time > g_LifeTime)
	{
		time = 0;
		m_ParticlesToSpawn = 1000000;
	}

	//m_ParticlesToSpawn += (float)g_EmitCount * GameTimer::DeltaTime();

	context.InsertResourceBarrier(m_pDrawArguments.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	context.InsertResourceBarrier(m_pEmitArguments.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	context.InsertResourceBarrier(m_pSimulateArguments.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	context.InsertResourceBarrier(m_pCountersBuffer.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	context.InsertResourceBarrier(m_pAliveList1.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	context.InsertResourceBarrier(m_pAliveList2.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	context.InsertResourceBarrier(m_pParticleBuffer.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	context.SetComputeRootSignature(m_pSimulateRS.get());

	D3D12_CPU_DESCRIPTOR_HANDLE uavs[] = {
		m_pCountersBuffer->GetUAV()->GetDescriptor(),
		m_pEmitArguments->GetUAV()->GetDescriptor(),
		m_pSimulateArguments->GetUAV()->GetDescriptor(),
		m_pDrawArguments->GetUAV()->GetDescriptor(),
		m_pDeadList->GetUAV()->GetDescriptor(),
		m_pAliveList1->GetUAV()->GetDescriptor(),
		m_pAliveList2->GetUAV()->GetDescriptor(),
		m_pParticleBuffer->GetUAV()->GetDescriptor(),
	};

	D3D12_CPU_DESCRIPTOR_HANDLE srvs[] = {
		m_pCountersBuffer->GetSRV()->GetDescriptor(),
		pResolvedDepth->GetSRV(),
		pNormals->GetSRV(),
	};

	context.SetDynamicDescriptors(1, 0, uavs, ARRAYSIZE(uavs));
	context.SetDynamicDescriptors(2, 0, srvs, ARRAYSIZE(srvs));

	{
		GPU_PROFILE_SCOPE("Prepare Arguments", &context);

		context.SetPipelineState(m_pPrepareArgumentsPS.get());
		struct Parameters
		{
			int32 EmitCount;
		} parameters;
		parameters.EmitCount = floor(m_ParticlesToSpawn);
		m_ParticlesToSpawn -= parameters.EmitCount;

		context.SetComputeDynamicConstantBufferView(0, &parameters, sizeof(Parameters));

		context.Dispatch(1, 1, 1);
		context.InsertUavBarrier();
		context.InsertResourceBarrier(m_pEmitArguments.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
		context.InsertResourceBarrier(m_pSimulateArguments.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
		context.FlushResourceBarriers();
	}
	{
		GPU_PROFILE_SCOPE("Emit", &context);

		context.SetPipelineState(m_pEmitPS.get());

		std::array<Vector4, 64> randomDirections;
		std::generate(randomDirections.begin(), randomDirections.end(), []()
			{
				Vector4 r = Vector4(Math::RandVector());
				r.y = Math::Lerp(0.6f, 0.8f, (float)abs(r.y));
				r.z = Math::Lerp(0.6f, 0.8f, (float)abs(r.z));
				r.Normalize();
				return r;
			});

		context.SetComputeDynamicConstantBufferView(0, randomDirections.data(), sizeof(Vector4) * (uint32)randomDirections.size());
		context.ExecuteIndirect(m_pSimpleDispatchCommandSignature->GetCommandSignature(), m_pEmitArguments.get());
		context.InsertUavBarrier();
	}
	{
		GPU_PROFILE_SCOPE("Simulate", &context);

		context.SetPipelineState(m_pSimulatePS.get());
		context.SetComputeRootSignature(m_pSimulateRS.get());

		struct Parameters
		{
			Matrix ViewProjection;
			float DeltaTime;
			float ParticleLifeTime;
			float Near;
			float Far;
		} parameters;
		parameters.DeltaTime = GameTimer::DeltaTime();
		parameters.ParticleLifeTime = g_LifeTime;
		parameters.ViewProjection = m_pGraphics->GetCamera()->GetViewProjection();
		parameters.Near = m_pGraphics->GetCamera()->GetNear();
		parameters.Far = m_pGraphics->GetCamera()->GetFar();

		context.SetComputeDynamicConstantBufferView(0, &parameters, sizeof(Parameters));
		context.ExecuteIndirect(m_pSimpleDispatchCommandSignature->GetCommandSignature(), m_pSimulateArguments.get());
		context.InsertUavBarrier();
	}
	{
		GPU_PROFILE_SCOPE("Simulate End", &context);

		context.SetPipelineState(m_pSimulateEndPS.get());
		context.Dispatch(1, 1, 1);
		context.InsertUavBarrier();
	}
	std::swap(m_pAliveList1, m_pAliveList2);
}

void GpuParticles::Render(CommandContext& context)
{
	{
		context.InsertResourceBarrier(m_pDrawArguments.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
		context.InsertResourceBarrier(m_pParticleBuffer.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		context.InsertResourceBarrier(m_pGraphics->GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_RENDER_TARGET);

		context.BeginRenderPass(RenderPassInfo(m_pGraphics->GetCurrentRenderTarget(), RenderPassAccess::Load_Store, m_pGraphics->GetDepthStencil(), RenderPassAccess::Load_DontCare));

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
			m_pAliveList1->GetSRV()->GetDescriptor()
		};
		context.SetDynamicDescriptors(1, 0, srvs, 2);
		context.ExecuteIndirect(m_pSimpleDrawCommandSignature->GetCommandSignature(), m_pDrawArguments.get(), false);
		context.EndRenderPass();
	}
}
