#include "stdafx.h"
#include "GpuParticles.h"
#include "Graphics/Core/GraphicsBuffer.h"
#include "Graphics/Core/CommandSignature.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/Core/Shader.h"
#include "Graphics/Core/PipelineState.h"
#include "Graphics/Core/RootSignature.h"
#include "Graphics/Core/CommandContext.h"
#include "Graphics/Core/GraphicsResource.h"
#include "Graphics/Core/Texture.h"
#include "Graphics/Core/ResourceViews.h"
#include "Graphics/Profiler.h"
#include "Graphics/ImGuiRenderer.h"
#include "Scene/Camera.h"
#include "../RenderGraph/RenderGraph.h"

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
{
	Initialize(pGraphics);
}

GpuParticles::~GpuParticles()
{
}

void GpuParticles::Initialize(Graphics* pGraphics)
{
	CommandContext* pContext = pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);

	m_pCountersBuffer = std::make_unique<Buffer>(pGraphics);
	m_pCountersBuffer->Create(BufferDesc::CreateByteAddress(sizeof(uint32) * 4));

	BufferDesc particleBufferDesc = BufferDesc::CreateStructured(cMaxParticleCount, sizeof(uint32));
	m_pAliveList1 = std::make_unique<Buffer>(pGraphics);
	m_pAliveList1->Create(particleBufferDesc);
	m_pAliveList2 = std::make_unique<Buffer>(pGraphics);
	m_pAliveList2->Create(particleBufferDesc);
	m_pDeadList = std::make_unique<Buffer>(pGraphics);
	m_pDeadList->Create(particleBufferDesc);
	std::vector<uint32> deadList(cMaxParticleCount);
	std::generate(deadList.begin(), deadList.end(), [n = 0]() mutable { return n++; });
	m_pDeadList->SetData(pContext, deadList.data(), sizeof(uint32) * deadList.size());
	uint32 aliveCount = cMaxParticleCount;
	m_pCountersBuffer->SetData(pContext, &aliveCount, sizeof(uint32), 0);

	m_pParticleBuffer = std::make_unique<Buffer>(pGraphics);
	m_pParticleBuffer->Create(BufferDesc::CreateStructured(cMaxParticleCount, sizeof(ParticleData)));

	m_pEmitArguments = std::make_unique<Buffer>(pGraphics);
	m_pEmitArguments->Create(BufferDesc::CreateIndirectArguments<uint32>(3));
	m_pSimulateArguments = std::make_unique<Buffer>(pGraphics);
	m_pSimulateArguments->Create(BufferDesc::CreateIndirectArguments<uint32>(3));
	m_pDrawArguments = std::make_unique<Buffer>(pGraphics);
	m_pDrawArguments->Create(BufferDesc::CreateIndirectArguments<uint32>(4));

	pContext->Execute(true);

	m_pSimpleDispatchCommandSignature = std::make_unique<CommandSignature>();
	m_pSimpleDispatchCommandSignature->AddDispatch();
	m_pSimpleDispatchCommandSignature->Finalize("Simple Dispatch", pGraphics->GetDevice());

	m_pSimpleDrawCommandSignature = std::make_unique<CommandSignature>();
	m_pSimpleDrawCommandSignature->AddDraw();
	m_pSimpleDrawCommandSignature->Finalize("Simple Draw", pGraphics->GetDevice());

	{
		Shader computeShader("ParticleSimulation.hlsl", ShaderType::Compute, "UpdateSimulationParameters");
		m_pSimulateRS = std::make_unique<RootSignature>();
		m_pSimulateRS->FinalizeFromShader("Particle Simulation RS", computeShader, pGraphics->GetDevice());
	}

	{
		Shader computeShader("ParticleSimulation.hlsl", ShaderType::Compute, "UpdateSimulationParameters");
		m_pPrepareArgumentsPS = std::make_unique<PipelineState>();
		m_pPrepareArgumentsPS->SetComputeShader(computeShader);
		m_pPrepareArgumentsPS->SetRootSignature(m_pSimulateRS->GetRootSignature());
		m_pPrepareArgumentsPS->Finalize("Prepare Particle Arguments PS", pGraphics->GetDevice());
	}
	{
		Shader computeShader("ParticleSimulation.hlsl", ShaderType::Compute, "Emit");
		m_pEmitPS = std::make_unique<PipelineState>();
		m_pEmitPS->SetComputeShader(computeShader);
		m_pEmitPS->SetRootSignature(m_pSimulateRS->GetRootSignature());
		m_pEmitPS->Finalize("Particle Emitter PS", pGraphics->GetDevice());
	}
	{
		Shader computeShader("ParticleSimulation.hlsl", ShaderType::Compute, "Simulate");
		m_pSimulatePS = std::make_unique<PipelineState>();
		m_pSimulatePS->SetComputeShader(computeShader);
		m_pSimulatePS->SetRootSignature(m_pSimulateRS->GetRootSignature());
		m_pSimulatePS->Finalize("Particle Simulation PS", pGraphics->GetDevice());
	}
	{
		Shader computeShader("ParticleSimulation.hlsl", ShaderType::Compute, "SimulateEnd");
		m_pSimulateEndPS = std::make_unique<PipelineState>();
		m_pSimulateEndPS->SetComputeShader(computeShader);
		m_pSimulateEndPS->SetRootSignature(m_pSimulateRS->GetRootSignature());
		m_pSimulateEndPS->Finalize("Particle Simulation End PS", pGraphics->GetDevice());
	}
	{
		Shader vertexShader("ParticleRendering.hlsl", ShaderType::Vertex, "VSMain");
		Shader pixelShader("ParticleRendering.hlsl", ShaderType::Pixel, "PSMain");

		m_pRenderParticlesRS = std::make_unique<RootSignature>();
		m_pRenderParticlesRS->FinalizeFromShader("Particle Rendering", vertexShader, pGraphics->GetDevice());

		m_pRenderParticlesPS = std::make_unique<PipelineState>();
		m_pRenderParticlesPS->SetVertexShader(vertexShader);
		m_pRenderParticlesPS->SetPixelShader(pixelShader);
		m_pRenderParticlesPS->SetRootSignature(m_pRenderParticlesRS->GetRootSignature());
		m_pRenderParticlesPS->SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
		m_pRenderParticlesPS->SetInputLayout(nullptr, 0);
		m_pRenderParticlesPS->SetDepthWrite(false);
		m_pRenderParticlesPS->SetBlendMode(BlendMode::Alpha, false);
		m_pRenderParticlesPS->SetCullMode(D3D12_CULL_MODE_NONE);
		m_pRenderParticlesPS->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		m_pRenderParticlesPS->SetRenderTargetFormat(Graphics::RENDER_TARGET_FORMAT, Graphics::DEPTH_STENCIL_FORMAT, pGraphics->GetMultiSampleCount());
		m_pRenderParticlesPS->Finalize("Particle Rendering PS", pGraphics->GetDevice());
	}

	pGraphics->GetImGui()->AddUpdateCallback(ImGuiCallbackDelegate::CreateLambda([]() {
		ImGui::Begin("Parameters");
		ImGui::Text("Particles");
		ImGui::Checkbox("Simulate", &g_Simulate);
		ImGui::SliderInt("Emit Count", &g_EmitCount, 0, cMaxParticleCount / 50);
		ImGui::SliderFloat("Life Time", &g_LifeTime, 0, 10);
		ImGui::End();
		}));
}

void GpuParticles::Simulate(RGGraph& graph, Texture* pResolvedDepth, const Camera& camera)
{
	if (!g_Simulate)
	{
		return;
	}

	static float time = 0;
	time += Time::DeltaTime();
	if (time > g_LifeTime)
	{
		time = 0;
		m_ParticlesToSpawn = 1000000;
	}

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
	};

	RG_GRAPH_SCOPE("Particle Simulation", graph);

	RGPassBuilder prepareArguments = graph.AddPass("Prepare Arguments");
	prepareArguments.Bind([=](CommandContext& context, const RGPassResources& passResources)
		{
			//m_ParticlesToSpawn += (float)g_EmitCount * GameTimer::DeltaTime();

			context.InsertResourceBarrier(m_pDrawArguments.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			context.InsertResourceBarrier(m_pEmitArguments.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			context.InsertResourceBarrier(m_pSimulateArguments.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			context.InsertResourceBarrier(m_pCountersBuffer.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			context.InsertResourceBarrier(m_pAliveList1.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			context.InsertResourceBarrier(m_pAliveList2.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			context.InsertResourceBarrier(m_pParticleBuffer.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			context.SetComputeRootSignature(m_pSimulateRS.get());
			context.SetDynamicDescriptors(1, 0, uavs, ARRAYSIZE(uavs));
			context.SetDynamicDescriptors(2, 0, srvs, ARRAYSIZE(srvs));

			context.SetPipelineState(m_pPrepareArgumentsPS.get());
			struct Parameters
			{
				int32 EmitCount;
			} parameters;
			parameters.EmitCount = (uint32)floor(m_ParticlesToSpawn);
			m_ParticlesToSpawn -= parameters.EmitCount;

			context.SetComputeDynamicConstantBufferView(0, &parameters, sizeof(Parameters));

			context.Dispatch(1);
			context.InsertUavBarrier();
			context.InsertResourceBarrier(m_pEmitArguments.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
			context.InsertResourceBarrier(m_pSimulateArguments.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
		});

	RGPassBuilder emit = graph.AddPass("Emit");
	emit.Bind([=](CommandContext& context, const RGPassResources& passResources)
		{
			context.SetComputeRootSignature(m_pSimulateRS.get());
			context.SetDynamicDescriptors(1, 0, uavs, ARRAYSIZE(uavs));
			context.SetDynamicDescriptors(2, 0, srvs, ARRAYSIZE(srvs));

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
			context.ExecuteIndirect(m_pSimpleDispatchCommandSignature.get(), m_pEmitArguments.get());
			context.InsertUavBarrier();
		});

	RGPassBuilder simulate = graph.AddPass("Simulate");
	simulate.Bind([=](CommandContext& context, const RGPassResources& passResources)
		{
			context.SetComputeRootSignature(m_pSimulateRS.get());
			context.SetDynamicDescriptors(1, 0, uavs, ARRAYSIZE(uavs));
			context.SetDynamicDescriptors(2, 0, srvs, ARRAYSIZE(srvs));

			context.SetPipelineState(m_pSimulatePS.get());

			struct Parameters
			{
				Matrix ViewProjection;
				Matrix ViewProjectionInv;
				Vector2 DimensionsInv;
				float DeltaTime;
				float ParticleLifeTime;
				float Near;
				float Far;
			} parameters;
			parameters.DimensionsInv.x = 1.0f / pResolvedDepth->GetWidth();
			parameters.DimensionsInv.y = 1.0f / pResolvedDepth->GetHeight();
			parameters.ViewProjectionInv = camera.GetProjectionInverse() * camera.GetViewInverse();
			parameters.DeltaTime = Time::DeltaTime();
			parameters.ParticleLifeTime = g_LifeTime;
			parameters.ViewProjection = camera.GetViewProjection();
			parameters.Near = camera.GetNear();
			parameters.Far = camera.GetFar();

			context.SetComputeDynamicConstantBufferView(0, &parameters, sizeof(Parameters));
			context.ExecuteIndirect(m_pSimpleDispatchCommandSignature.get(), m_pSimulateArguments.get());
			context.InsertUavBarrier();
		});

	RGPassBuilder simulateEnd = graph.AddPass("Simulate End");
	simulateEnd.Bind([=](CommandContext& context, const RGPassResources& passResources)
		{
			context.SetComputeRootSignature(m_pSimulateRS.get());
			context.SetDynamicDescriptors(1, 0, uavs, ARRAYSIZE(uavs));
			context.SetDynamicDescriptors(2, 0, srvs, ARRAYSIZE(srvs));

			context.SetPipelineState(m_pSimulateEndPS.get());
			context.Dispatch(1);
			context.InsertUavBarrier();
		});

	std::swap(m_pAliveList1, m_pAliveList2);
}

void GpuParticles::Render(RGGraph& graph, Texture* pTarget, Texture* pDepth, const Camera& camera)
{
	RGPassBuilder renderParticles = graph.AddPass("Render Particles");
	renderParticles.Bind([=](CommandContext& context, const RGPassResources& resources)
		{
			context.InsertResourceBarrier(m_pDrawArguments.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
			context.InsertResourceBarrier(m_pParticleBuffer.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(pTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);

			context.BeginRenderPass(RenderPassInfo(pTarget, RenderPassAccess::Load_Store, pDepth, RenderPassAccess::Load_DontCare));

			context.SetPipelineState(m_pRenderParticlesPS.get());
			context.SetGraphicsRootSignature(m_pRenderParticlesRS.get());

			Vector2 screenDimensions((float)pTarget->GetWidth(), (float)pTarget->GetHeight());
			context.SetViewport(FloatRect(0, 0, (float)screenDimensions.x, (float)screenDimensions.y));
			context.SetScissorRect(FloatRect(0, 0, (float)screenDimensions.x, (float)screenDimensions.y));

			struct FrameData
			{
				Matrix ViewInverse;
				Matrix View;
				Matrix Projection;
			} frameData;
			frameData.ViewInverse = camera.GetViewInverse();
			frameData.View = camera.GetView();
			frameData.Projection = camera.GetProjection();

			context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context.SetDynamicConstantBufferView(0, &frameData, sizeof(FrameData));

			D3D12_CPU_DESCRIPTOR_HANDLE srvs[] = {
				m_pParticleBuffer->GetSRV()->GetDescriptor(),
				m_pAliveList1->GetSRV()->GetDescriptor()
			};
			context.SetDynamicDescriptors(1, 0, srvs, 2);
			context.ExecuteIndirect(m_pSimpleDrawCommandSignature.get(), m_pDrawArguments.get(), false);
			context.EndRenderPass();
		});
}
