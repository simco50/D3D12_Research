#include "stdafx.h"
#include "GpuParticles.h"
#include "Graphics/Core/Buffer.h"
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
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/SceneView.h"

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

GpuParticles::GpuParticles(GraphicsDevice* pDevice)
	: m_pDevice(pDevice)
{
	CommandContext* pContext = pDevice->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);

	m_pCountersBuffer = pDevice->CreateBuffer(BufferDesc::CreateByteAddress(sizeof(uint32) * 4), "Particles Counter");
	BufferDesc particleBufferDesc = BufferDesc::CreateStructured(cMaxParticleCount, sizeof(uint32));
	m_pAliveList1 = pDevice->CreateBuffer(particleBufferDesc, "Particles Alive List 1");
	m_pAliveList2 = pDevice->CreateBuffer(particleBufferDesc, "Particles Alive List 2");
	m_pDeadList = pDevice->CreateBuffer(particleBufferDesc, "Particles Dead List");
	std::vector<uint32> deadList(cMaxParticleCount);
	std::generate(deadList.begin(), deadList.end(), [n = 0]() mutable { return n++; });
	pContext->InitializeBuffer(m_pDeadList, deadList.data(), sizeof(uint32) * deadList.size());
	uint32 aliveCount = cMaxParticleCount;
	pContext->InitializeBuffer(m_pCountersBuffer, &aliveCount, sizeof(uint32), 0);

	m_pParticleBuffer = pDevice->CreateBuffer(BufferDesc::CreateStructured(cMaxParticleCount, sizeof(ParticleData)), "Particle Buffer");

	m_pEmitArguments = pDevice->CreateBuffer(BufferDesc::CreateIndirectArguments<uint32>(3), "Emit Indirect Arguments");
	m_pSimulateArguments = pDevice->CreateBuffer(BufferDesc::CreateIndirectArguments<uint32>(3), "Simulate Indirect Arguments");
	m_pDrawArguments = pDevice->CreateBuffer(BufferDesc::CreateIndirectArguments<uint32>(4), "Draw Indirect Arguments");

	pContext->Execute(true);

	{
		m_pSimulateRS = new RootSignature(pDevice);
		m_pSimulateRS->FinalizeFromShader("Particle Simulation", pDevice->GetShader("ParticleSimulation.hlsl", ShaderType::Compute, "UpdateSimulationParameters"));
	}
	{
		m_pPrepareArgumentsPS = pDevice->CreatePipeline(pDevice->GetShader("ParticleSimulation.hlsl", ShaderType::Compute, "UpdateSimulationParameters"), m_pSimulateRS);
		m_pEmitPS = pDevice->CreatePipeline(pDevice->GetShader("ParticleSimulation.hlsl", ShaderType::Compute, "Emit"), m_pSimulateRS);
		m_pSimulatePS = pDevice->CreatePipeline(pDevice->GetShader("ParticleSimulation.hlsl", ShaderType::Compute, "Simulate"), m_pSimulateRS);
		m_pSimulateEndPS = pDevice->CreatePipeline(pDevice->GetShader("ParticleSimulation.hlsl", ShaderType::Compute, "SimulateEnd"), m_pSimulateRS);
	}
	{
		Shader* pVertexShader = pDevice->GetShader("ParticleRendering.hlsl", ShaderType::Vertex, "VSMain");
		Shader* pPixelShader = pDevice->GetShader("ParticleRendering.hlsl", ShaderType::Pixel, "PSMain");

		m_pRenderParticlesRS = new RootSignature(pDevice);
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
		psoDesc.SetRenderTargetFormat(DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_D32_FLOAT, 1);
		psoDesc.SetName("Particle Rendering PS");
		m_pRenderParticlesPS = pDevice->CreatePipeline(psoDesc);
	}
}

void GpuParticles::Simulate(RGGraph& graph, const SceneView& resources, Texture* pResolvedDepth)
{
	if (ImGui::Begin("Parameters"))
	{
		if (ImGui::CollapsingHeader("Particles"))
		{
			ImGui::Checkbox("Enabled", &g_Enabled);
			ImGui::Checkbox("Simulate", &g_Simulate);
			ImGui::SliderInt("Emit Count", &g_EmitCount, 0, cMaxParticleCount / 50);
			ImGui::SliderFloat("Life Time", &g_LifeTime, 0, 10);
		}
	}
	ImGui::End();

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

			context.InsertResourceBarrier(m_pDrawArguments, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			context.InsertResourceBarrier(m_pEmitArguments, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			context.InsertResourceBarrier(m_pSimulateArguments, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			context.InsertResourceBarrier(m_pCountersBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			context.InsertResourceBarrier(m_pAliveList1, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			context.InsertResourceBarrier(m_pAliveList2, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			context.InsertResourceBarrier(m_pParticleBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			context.SetComputeRootSignature(m_pSimulateRS);

			context.SetPipelineState(m_pPrepareArgumentsPS);
			struct Parameters
			{
				int32 EmitCount;
			} parameters;
			parameters.EmitCount = (uint32)floor(m_ParticlesToSpawn);
			m_ParticlesToSpawn -= parameters.EmitCount;

			context.SetRootCBV(0, parameters);
			context.SetRootCBV(1, GetViewUniforms(resources));

			context.BindResources(2, 0, uavs, ARRAYSIZE(uavs));
			context.BindResources(3, 0, srvs, ARRAYSIZE(srvs));

			context.Dispatch(1);
			context.InsertUavBarrier();
			context.InsertResourceBarrier(m_pEmitArguments, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
			context.InsertResourceBarrier(m_pSimulateArguments, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
		});

	RGPassBuilder emit = graph.AddPass("Emit");
	emit.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
		{
			context.SetComputeRootSignature(m_pSimulateRS);
			context.SetPipelineState(m_pEmitPS);

			struct Parameters
			{
				Vector3 Origin;
			};
			Parameters parameters{};

			parameters.Origin = Vector3(150, 3, 0);

			context.SetRootCBV(0, parameters);
			context.SetRootCBV(1, GetViewUniforms(resources));

			context.BindResources(2, 0, uavs, ARRAYSIZE(uavs));
			context.BindResources(3, 0, srvs, ARRAYSIZE(srvs));

			context.ExecuteIndirect(GraphicsCommon::pIndirectDispatchSignature, 1, m_pEmitArguments, m_pEmitArguments);
			context.InsertUavBarrier();
		});

	RGPassBuilder simulate = graph.AddPass("Simulate");
	simulate.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
		{
			context.SetComputeRootSignature(m_pSimulateRS);
			context.SetPipelineState(m_pSimulatePS);

			struct Parameters
			{
				float DeltaTime;
				float ParticleLifeTime;
			} parameters;
			parameters.DeltaTime = Time::DeltaTime();
			parameters.ParticleLifeTime = g_LifeTime;

			context.SetRootCBV(0, parameters);
			context.SetRootCBV(1, GetViewUniforms(resources));

			context.BindResources(2, 0, uavs, ARRAYSIZE(uavs));
			context.BindResources(3, 0, srvs, ARRAYSIZE(srvs));

			context.ExecuteIndirect(GraphicsCommon::pIndirectDispatchSignature, 1, m_pSimulateArguments, nullptr);
			context.InsertUavBarrier();
		});

	RGPassBuilder simulateEnd = graph.AddPass("Simulate End");
	simulateEnd.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
		{
			context.InsertResourceBarrier(m_pCountersBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

			context.SetComputeRootSignature(m_pSimulateRS);

			context.SetRootCBV(1, GetViewUniforms(resources));

			context.BindResources(2, 0, uavs, ARRAYSIZE(uavs));
			context.BindResources(3, 0, srvs, ARRAYSIZE(srvs));

			context.SetPipelineState(m_pSimulateEndPS);
			context.Dispatch(1);
			context.InsertUavBarrier();
		});

	std::swap(m_pAliveList1, m_pAliveList2);
}

void GpuParticles::Render(RGGraph& graph, const SceneView& resources, Texture* pTarget, Texture* pDepth)
{
	if (!g_Enabled)
	{
		return;
	}

	RGPassBuilder renderParticles = graph.AddPass("Render Particles");
	renderParticles.Bind([=](CommandContext& context, const RGPassResources& /*resources*/)
		{
			context.InsertResourceBarrier(m_pDrawArguments, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
			context.InsertResourceBarrier(m_pParticleBuffer, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pAliveList1, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
			context.InsertResourceBarrier(pTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
			context.InsertResourceBarrier(pDepth, D3D12_RESOURCE_STATE_DEPTH_READ);

			context.BeginRenderPass(RenderPassInfo(pTarget, RenderPassAccess::Load_Store, pDepth, RenderPassAccess::Load_Store, false));

			context.SetPipelineState(m_pRenderParticlesPS);
			context.SetGraphicsRootSignature(m_pRenderParticlesRS);

			context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context.SetRootCBV(0, GetViewUniforms(resources, pTarget));

			D3D12_CPU_DESCRIPTOR_HANDLE srvs[] = {
				m_pParticleBuffer->GetSRV()->GetDescriptor(),
				m_pAliveList1->GetSRV()->GetDescriptor()
			};
			context.BindResources(1, 0, srvs, 2);
			context.ExecuteIndirect(GraphicsCommon::pIndirectDrawSignature, 1, m_pDrawArguments, nullptr);
			context.EndRenderPass();
		});
}
