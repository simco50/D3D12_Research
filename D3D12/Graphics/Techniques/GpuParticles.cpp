#include "stdafx.h"
#include "GpuParticles.h"
#include "Graphics/Core/GraphicsBuffer.h"
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

static bool g_Enabled = true;
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

GpuParticles::GpuParticles(ShaderManager* pShaderManager, GraphicsDevice* pDevice)
{
	Initialize(pShaderManager, pDevice);
}

GpuParticles::~GpuParticles()
{
}

void GpuParticles::Initialize(ShaderManager* pShaderManager, GraphicsDevice* pDevice)
{
	CommandContext* pContext = pDevice->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);

	m_pCountersBuffer = std::make_unique<Buffer>(pDevice, "Particles Counter");
	m_pCountersBuffer->Create(BufferDesc::CreateByteAddress(sizeof(uint32) * 4));

	BufferDesc particleBufferDesc = BufferDesc::CreateStructured(cMaxParticleCount, sizeof(uint32));
	m_pAliveList1 = std::make_unique<Buffer>(pDevice, "Particles Alive List 1");
	m_pAliveList1->Create(particleBufferDesc);
	m_pAliveList2 = std::make_unique<Buffer>(pDevice, "Particles Alive List 2");
	m_pAliveList2->Create(particleBufferDesc);
	m_pDeadList = std::make_unique<Buffer>(pDevice, "Particles Dead List");
	m_pDeadList->Create(particleBufferDesc);
	std::vector<uint32> deadList(cMaxParticleCount);
	std::generate(deadList.begin(), deadList.end(), [n = 0]() mutable { return n++; });
	m_pDeadList->SetData(pContext, deadList.data(), sizeof(uint32) * deadList.size());
	uint32 aliveCount = cMaxParticleCount;
	m_pCountersBuffer->SetData(pContext, &aliveCount, sizeof(uint32), 0);

	m_pParticleBuffer = std::make_unique<Buffer>(pDevice, "Particle Buffer");
	m_pParticleBuffer->Create(BufferDesc::CreateStructured(cMaxParticleCount, sizeof(ParticleData)));

	m_pEmitArguments = std::make_unique<Buffer>(pDevice, "Emit Indirect Arguments");
	m_pEmitArguments->Create(BufferDesc::CreateIndirectArguments<uint32>(3));
	m_pSimulateArguments = std::make_unique<Buffer>(pDevice, "Simulate Indirect Arguments");
	m_pSimulateArguments->Create(BufferDesc::CreateIndirectArguments<uint32>(3));
	m_pDrawArguments = std::make_unique<Buffer>(pDevice, "Draw Indirect Arguments");
	m_pDrawArguments->Create(BufferDesc::CreateIndirectArguments<uint32>(4));

	pContext->Execute(true);

	m_pSimpleDispatchCommandSignature = std::make_unique<CommandSignature>(pDevice);
	m_pSimpleDispatchCommandSignature->AddDispatch();
	m_pSimpleDispatchCommandSignature->Finalize("Simple Dispatch");

	m_pSimpleDrawCommandSignature = std::make_unique<CommandSignature>(pDevice);
	m_pSimpleDrawCommandSignature->AddDraw();
	m_pSimpleDrawCommandSignature->Finalize("Simple Draw");

	{
		Shader* pComputeShader = pShaderManager->GetShader("ParticleSimulation.hlsl", ShaderType::Compute, "UpdateSimulationParameters");
		m_pSimulateRS = std::make_unique<RootSignature>(pDevice);
		m_pSimulateRS->FinalizeFromShader("Particle Simulation", pComputeShader);
	}

	{
		Shader* pComputeShader = pShaderManager->GetShader("ParticleSimulation.hlsl", ShaderType::Compute, "UpdateSimulationParameters");
		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetRootSignature(m_pSimulateRS->GetRootSignature());
		psoDesc.SetName("Prepare Particle Arguments");
		m_pPrepareArgumentsPS = pDevice->CreatePipeline(psoDesc);
	}
	{
		Shader* pComputeShader = pShaderManager->GetShader("ParticleSimulation.hlsl", ShaderType::Compute, "Emit");
		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetRootSignature(m_pSimulateRS->GetRootSignature());
		psoDesc.SetName("Particle Emitter");
		m_pEmitPS = pDevice->CreatePipeline(psoDesc);
	}
	{
		Shader* pComputeShader = pShaderManager->GetShader("ParticleSimulation.hlsl", ShaderType::Compute, "Simulate");
		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetRootSignature(m_pSimulateRS->GetRootSignature());
		psoDesc.SetName("Particle Simulation");
		m_pSimulatePS = pDevice->CreatePipeline(psoDesc);
	}
	{
		Shader* pComputeShader = pShaderManager->GetShader("ParticleSimulation.hlsl", ShaderType::Compute, "SimulateEnd");
		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetRootSignature(m_pSimulateRS->GetRootSignature());
		psoDesc.SetName("Particle Simulation End");
		m_pSimulateEndPS = pDevice->CreatePipeline(psoDesc);
	}
	{
		Shader* pVertexShader = pShaderManager->GetShader("ParticleRendering.hlsl", ShaderType::Vertex, "VSMain");
		Shader* pPixelShader = pShaderManager->GetShader("ParticleRendering.hlsl", ShaderType::Pixel, "PSMain");

		m_pRenderParticlesRS = std::make_unique<RootSignature>(pDevice);
		m_pRenderParticlesRS->FinalizeFromShader("Particle Rendering", pVertexShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetVertexShader(pVertexShader);
		psoDesc.SetPixelShader(pPixelShader);
		psoDesc.SetRootSignature(m_pRenderParticlesRS->GetRootSignature());
		psoDesc.SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
		psoDesc.SetDepthWrite(false);
		psoDesc.SetBlendMode(BlendMode::Alpha, false);
		psoDesc.SetCullMode(D3D12_CULL_MODE_NONE);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		psoDesc.SetRenderTargetFormat(Graphics::RENDER_TARGET_FORMAT, Graphics::DEPTH_STENCIL_FORMAT, pDevice->GetMultiSampleCount());
		psoDesc.SetName("Particle Rendering PS");
		m_pRenderParticlesPS = pDevice->CreatePipeline(psoDesc);
	}

	pDevice->GetImGui()->AddUpdateCallback(ImGuiCallbackDelegate::CreateLambda([]() {
		ImGui::Begin("Parameters");
		ImGui::Text("Particles");
		ImGui::Checkbox("Enabled", &g_Enabled);
		ImGui::Checkbox("Simulate", &g_Simulate);
		ImGui::SliderInt("Emit Count", &g_EmitCount, 0, cMaxParticleCount / 50);
		ImGui::SliderFloat("Life Time", &g_LifeTime, 0, 10);
		ImGui::End();
		}));
}

void GpuParticles::Simulate(RGGraph& graph, Texture* pResolvedDepth, const Camera& camera)
{
	if (!g_Simulate || !g_Enabled)
	{
		return;
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
		pResolvedDepth->GetSRV()->GetDescriptor(),
	};

	RG_GRAPH_SCOPE("Particle Simulation", graph);

	RGPassBuilder prepareArguments = graph.AddPass("Prepare Arguments");
	prepareArguments.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
		{
			m_ParticlesToSpawn += (float)g_EmitCount * Time::DeltaTime();

			context.InsertResourceBarrier(m_pDrawArguments.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			context.InsertResourceBarrier(m_pEmitArguments.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			context.InsertResourceBarrier(m_pSimulateArguments.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			context.InsertResourceBarrier(m_pCountersBuffer.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			context.InsertResourceBarrier(m_pAliveList1.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			context.InsertResourceBarrier(m_pAliveList2.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			context.InsertResourceBarrier(m_pParticleBuffer.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			context.SetComputeRootSignature(m_pSimulateRS.get());
			context.BindResources(1, 0, uavs, ARRAYSIZE(uavs));
			context.BindResources(2, 0, srvs, ARRAYSIZE(srvs));

			context.SetPipelineState(m_pPrepareArgumentsPS);
			struct Parameters
			{
				int32 EmitCount;
			} parameters;
			parameters.EmitCount = (uint32)floor(m_ParticlesToSpawn);
			m_ParticlesToSpawn -= parameters.EmitCount;

			context.SetComputeDynamicConstantBufferView(0, parameters);

			context.Dispatch(1);
			context.InsertUavBarrier();
			context.InsertResourceBarrier(m_pEmitArguments.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
			context.InsertResourceBarrier(m_pSimulateArguments.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
		});

	RGPassBuilder emit = graph.AddPass("Emit");
	emit.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
		{
			context.SetComputeRootSignature(m_pSimulateRS.get());
			context.BindResources(1, 0, uavs, ARRAYSIZE(uavs));
			context.BindResources(2, 0, srvs, ARRAYSIZE(srvs));

			context.SetPipelineState(m_pEmitPS);

			struct Parameters
			{
				std::array<Vector4, 64> RandomDirections;
				Vector3 Origin;
			};
			Parameters parameters{};

			std::generate(parameters.RandomDirections.begin(), parameters.RandomDirections.end(), []()
				{
					Vector4 r = Vector4(Math::RandVector());
					r.y = Math::Lerp(0.6f, 0.8f, (float)abs(r.y));
					r.z = Math::Lerp(0.6f, 0.8f, (float)abs(r.z));
					r.Normalize();
					return r;
				}); 
			parameters.Origin = Vector3(150, 3, 0);

			context.SetComputeDynamicConstantBufferView(0, parameters);
			context.ExecuteIndirect(m_pSimpleDispatchCommandSignature.get(), 1, m_pEmitArguments.get(), m_pEmitArguments.get());
			context.InsertUavBarrier();
		});

	RGPassBuilder simulate = graph.AddPass("Simulate");
	simulate.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
		{
			context.SetComputeRootSignature(m_pSimulateRS.get());
			context.BindResources(1, 0, uavs, ARRAYSIZE(uavs));
			context.BindResources(2, 0, srvs, ARRAYSIZE(srvs));

			context.SetPipelineState(m_pSimulatePS);

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
			parameters.ViewProjectionInv = camera.GetViewProjectionInverse();
			parameters.DeltaTime = Time::DeltaTime();
			parameters.ParticleLifeTime = g_LifeTime;
			parameters.ViewProjection = camera.GetViewProjection();
			parameters.Near = camera.GetNear();
			parameters.Far = camera.GetFar();

			context.SetComputeDynamicConstantBufferView(0, parameters);
			context.ExecuteIndirect(m_pSimpleDispatchCommandSignature.get(), 1, m_pSimulateArguments.get(), nullptr);
			context.InsertUavBarrier();
		});

	RGPassBuilder simulateEnd = graph.AddPass("Simulate End");
	simulateEnd.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
		{
			context.InsertResourceBarrier(m_pCountersBuffer.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

			context.SetComputeRootSignature(m_pSimulateRS.get());
			context.BindResources(1, 0, uavs, ARRAYSIZE(uavs));
			context.BindResources(2, 0, srvs, ARRAYSIZE(srvs));

			context.SetPipelineState(m_pSimulateEndPS);
			context.Dispatch(1);
			context.InsertUavBarrier();
		});

	std::swap(m_pAliveList1, m_pAliveList2);
}

void GpuParticles::Render(RGGraph& graph, Texture* pTarget, Texture* pDepth, const Camera& camera)
{
	if (!g_Enabled)
	{
		return;
	}

	RGPassBuilder renderParticles = graph.AddPass("Render Particles");
	renderParticles.Bind([=](CommandContext& context, const RGPassResources& /*resources*/)
		{
			context.InsertResourceBarrier(m_pDrawArguments.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
			context.InsertResourceBarrier(m_pParticleBuffer.get(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pAliveList1.get(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
			context.InsertResourceBarrier(pTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);

			context.BeginRenderPass(RenderPassInfo(pTarget, RenderPassAccess::Load_Store, pDepth, RenderPassAccess::Load_Store, false));

			context.SetPipelineState(m_pRenderParticlesPS);
			context.SetGraphicsRootSignature(m_pRenderParticlesRS.get());

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
			context.SetGraphicsDynamicConstantBufferView(0, frameData);

			D3D12_CPU_DESCRIPTOR_HANDLE srvs[] = {
				m_pParticleBuffer->GetSRV()->GetDescriptor(),
				m_pAliveList1->GetSRV()->GetDescriptor()
			};
			context.BindResources(1, 0, srvs, 2);
			context.ExecuteIndirect(m_pSimpleDrawCommandSignature.get(), 1, m_pDrawArguments.get(), nullptr);
			context.EndRenderPass();
		});
}
