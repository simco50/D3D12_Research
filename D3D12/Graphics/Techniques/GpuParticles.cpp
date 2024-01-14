#include "stdafx.h"
#include "GpuParticles.h"
#include "Graphics/RHI/Buffer.h"
#include "Graphics/RHI/Graphics.h"
#include "Graphics/RHI/PipelineState.h"
#include "Graphics/RHI/RootSignature.h"
#include "Graphics/RHI/CommandContext.h"
#include "Graphics/RHI/GraphicsResource.h"
#include "Graphics/RHI/Texture.h"
#include "Graphics/RHI/ResourceViews.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/SceneView.h"

static bool g_Enabled = false;
static int32 g_EmitCount = 30;
static float g_LifeTime = 4.0f;
static bool g_Simulate = true;

static constexpr uint32 cMaxParticleCount = 1 << 16;

struct ParticleData
{
	Vector3 Position;
	float LifeTime;
	Vector3 Velocity;
	float Size;
};

struct ParticleBlackboardData
{
	RGBuffer* pIndirectDrawArguments;
	RGBuffer* pParticlesBuffer;
	RGBuffer* pAliveList;
};
RG_BLACKBOARD_DATA(ParticleBlackboardData);

GpuParticles::GpuParticles(GraphicsDevice* pDevice)
{
	{
		m_pCommonRS = new RootSignature(pDevice);
		m_pCommonRS->AddRootConstants(0, 4);
		m_pCommonRS->AddRootCBV(100);
		m_pCommonRS->AddDescriptorTable(0, 6, D3D12_DESCRIPTOR_RANGE_TYPE_UAV);
		m_pCommonRS->AddDescriptorTable(0, 6, D3D12_DESCRIPTOR_RANGE_TYPE_SRV);
		m_pCommonRS->Finalize("Particles");
	}
	{
		m_pPrepareArgumentsPS = pDevice->CreateComputePipeline(m_pCommonRS, "ParticleSimulation.hlsl", "UpdateSimulationParameters");
		m_pEmitPS = pDevice->CreateComputePipeline(m_pCommonRS, "ParticleSimulation.hlsl", "Emit");
		m_pSimulatePS = pDevice->CreateComputePipeline(m_pCommonRS, "ParticleSimulation.hlsl", "Simulate");
		m_pSimulateEndPS = pDevice->CreateComputePipeline(m_pCommonRS, "ParticleSimulation.hlsl", "SimulateEnd");
		m_pInitializeBuffersPSO = pDevice->CreateComputePipeline(m_pCommonRS, "ParticleSimulation.hlsl", "InitializeDataCS");
	}
	{
		PipelineStateInitializer psoDesc;
		psoDesc.SetVertexShader("ParticleRendering.hlsl", "VSMain");
		psoDesc.SetPixelShader("ParticleRendering.hlsl", "PSMain");
		psoDesc.SetRootSignature(m_pCommonRS);
		psoDesc.SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
		psoDesc.SetDepthWrite(true);
		psoDesc.SetBlendMode(BlendMode::Alpha, false);
		psoDesc.SetCullMode(D3D12_CULL_MODE_NONE);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		psoDesc.SetRenderTargetFormats(GraphicsCommon::GBufferFormat, GraphicsCommon::DepthStencilFormat, 1);
		psoDesc.SetName("Particle Rendering PS");
		m_pRenderParticlesPS = pDevice->CreatePipeline(psoDesc);
	}
}

struct IndirectArgs
{
	D3D12_DISPATCH_ARGUMENTS EmitArgs;
	D3D12_DISPATCH_ARGUMENTS SimulateArgs;
	D3D12_DRAW_ARGUMENTS DrawArgs;
};

void GpuParticles::Simulate(RGGraph& graph, const SceneView* pView, RGTexture* pDepth)
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

	if (!g_Enabled)
		return;

	RG_GRAPH_SCOPE("Particle Simulation", graph);

	bool needsInitialize = !m_pParticleBuffer;

	RGBuffer* pIndirectArgs = graph.Create("Indirect Arguments", BufferDesc::CreateIndirectArguments<IndirectArgs>());
	BufferDesc particleBufferDesc = BufferDesc::CreateStructured(cMaxParticleCount, sizeof(uint32));
	RGBuffer* pNewAliveList = graph.Create("New Alive List", particleBufferDesc);
	RGBuffer* pParticlesBuffer = RGUtils::CreatePersistent(graph, "Particles Buffer", BufferDesc::CreateStructured(cMaxParticleCount, sizeof(ParticleData)), &m_pParticleBuffer, true);
	RGBuffer* pCurrentAliveList = RGUtils::CreatePersistent(graph, "Current Alive List", particleBufferDesc, &m_pAliveList, false);
	RGBuffer* pDeadList = RGUtils::CreatePersistent(graph, "Dead List", particleBufferDesc, &m_pDeadList, true);
	RGBuffer* pCountersBuffer = RGUtils::CreatePersistent(graph, "Particles Counter", BufferDesc::CreateByteAddress(sizeof(uint32) * 4), &m_pCountersBuffer, true);
	graph.Export(pNewAliveList, &m_pAliveList);

	ParticleBlackboardData& data = graph.Blackboard.Add<ParticleBlackboardData>();
	data.pIndirectDrawArguments = pIndirectArgs;
	data.pParticlesBuffer = pParticlesBuffer;
	data.pAliveList = pNewAliveList;

	if (needsInitialize)
	{
		graph.AddPass("Initialize", RGPassFlag::Compute)
			.Write({ pDeadList, pCountersBuffer })
			.Bind([=](CommandContext& context)
				{
					context.SetComputeRootSignature(m_pCommonRS);
					context.SetPipelineState(m_pInitializeBuffersPSO);

					struct
					{
						uint32 MaxNumParticles;
					} params;
					params.MaxNumParticles = cMaxParticleCount;

					context.BindRootCBV(0, params);
					context.BindResources(2, {
						pCountersBuffer->Get()->GetUAV(),
						pDeadList->Get()->GetUAV(),
						});

					context.Dispatch(ComputeUtils::GetNumThreadGroups(cMaxParticleCount, 32));
					context.InsertUAVBarrier();
				});
	}

	if (g_Simulate)
	{
		graph.AddPass("Prepare Arguments", RGPassFlag::Compute)
			.Read(pDepth)
			.Write({ pCountersBuffer, pIndirectArgs })
			.Bind([=](CommandContext& context)
				{
					m_ParticlesToSpawn += (float)g_EmitCount * Time::DeltaTime();

					context.SetComputeRootSignature(m_pCommonRS);
					context.SetPipelineState(m_pPrepareArgumentsPS);
					struct
					{
						int32 EmitCount;
					} parameters;
					parameters.EmitCount = (int32)Math::Floor(m_ParticlesToSpawn);
					m_ParticlesToSpawn -= parameters.EmitCount;

					context.BindRootCBV(0, parameters);
					context.BindResources(2, {
						pCountersBuffer->Get()->GetUAV(),
						nullptr,
						nullptr,
						nullptr,
						nullptr,
						pIndirectArgs->Get()->GetUAV(),
						}, 0);

					context.Dispatch(1);
					context.InsertUAVBarrier();
				});

		graph.AddPass("Emit", RGPassFlag::Compute | RGPassFlag::NeverCull)
			.Read({ pDepth, pIndirectArgs, pDeadList })
			.Write({ pParticlesBuffer, pCountersBuffer, pCurrentAliveList })
			.Bind([=](CommandContext& context)
				{
					context.SetComputeRootSignature(m_pCommonRS);
					context.SetPipelineState(m_pEmitPS);

					struct
					{
						Vector3 Origin;
					} parameters;

					parameters.Origin = Vector3(1, 1, 0);

					context.BindRootCBV(0, parameters);
					context.BindRootCBV(1, Renderer::GetViewUniforms(pView));
					context.BindResources(2, {
						pCountersBuffer->Get()->GetUAV(),
						nullptr,
						pCurrentAliveList->Get()->GetUAV(),
						nullptr,
						pParticlesBuffer->Get()->GetUAV(),
						});
					context.BindResources(3, {
						nullptr,
						pDeadList->Get()->GetSRV(),
						});

					context.ExecuteIndirect(GraphicsCommon::pIndirectDispatchSignature, 1, pIndirectArgs->Get(), nullptr, offsetof(IndirectArgs, EmitArgs));
					context.InsertUAVBarrier();
				});

		graph.AddPass("Simulate", RGPassFlag::Compute | RGPassFlag::NeverCull)
			.Read({ pDepth, pIndirectArgs, pCurrentAliveList })
			.Write({ pCountersBuffer, pDeadList, pNewAliveList, pParticlesBuffer })
			.Bind([=](CommandContext& context)
				{
					context.SetComputeRootSignature(m_pCommonRS);
					context.SetPipelineState(m_pSimulatePS);

					struct
					{
						float DeltaTime;
						float ParticleLifeTime;
					} parameters;
					parameters.DeltaTime = Time::DeltaTime();
					parameters.ParticleLifeTime = g_LifeTime;

					context.BindRootCBV(0, parameters);
					context.BindRootCBV(1, Renderer::GetViewUniforms(pView));
					context.BindResources(2, {
						pCountersBuffer->Get()->GetUAV(),
						pDeadList->Get()->GetUAV(),
						nullptr,
						pNewAliveList->Get()->GetUAV(),
						pParticlesBuffer->Get()->GetUAV(),
						});
					context.BindResources(3, {
						nullptr,
						nullptr,
						pCurrentAliveList->Get()->GetSRV(),
						pDepth->Get()->GetSRV(),
						});

					context.ExecuteIndirect(GraphicsCommon::pIndirectDispatchSignature, 1, pIndirectArgs->Get(), nullptr, offsetof(IndirectArgs, SimulateArgs));
				});
	}

	graph.AddPass("Simulate End", RGPassFlag::Compute)
		.Read({ pCountersBuffer })
		.Write({ pIndirectArgs })
		.Bind([=](CommandContext& context)
			{
				context.SetComputeRootSignature(m_pCommonRS);
				context.SetPipelineState(m_pSimulateEndPS);

				context.BindRootCBV(1, Renderer::GetViewUniforms(pView));
				context.BindResources(2, {
					pIndirectArgs->Get()->GetUAV(),
					}, 5);
				context.BindResources(3, {
					pCountersBuffer->Get()->GetSRV(),
					});

				context.Dispatch(1);
				context.InsertUAVBarrier();
			});
}

void GpuParticles::Render(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures)
{
	if (!g_Enabled)
	{
		return;
	}

	const ParticleBlackboardData* pData = graph.Blackboard.TryGet<ParticleBlackboardData>();
	if (!pData)
		return;

	graph.AddPass("Render Particles", RGPassFlag::Raster)
		.Read(pData->pIndirectDrawArguments)
		.Read({ pData->pParticlesBuffer, pData->pAliveList })
		.DepthStencil(sceneTextures.pDepth)
		.RenderTarget(sceneTextures.pColorTarget)
		.RenderTarget(sceneTextures.pNormals)
		.RenderTarget(sceneTextures.pRoughness)
		.Bind([=](CommandContext& context)
			{
				context.SetPipelineState(m_pRenderParticlesPS);
				context.SetGraphicsRootSignature(m_pCommonRS);

				context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				context.BindRootCBV(1, Renderer::GetViewUniforms(pView, sceneTextures.pColorTarget->Get()));
				context.BindResources(3, {
					pData->pParticlesBuffer->Get()->GetSRV(),
					pData->pAliveList->Get()->GetSRV()
					});
				context.ExecuteIndirect(GraphicsCommon::pIndirectDrawSignature, 1, pData->pIndirectDrawArguments->Get(), nullptr, offsetof(IndirectArgs, DrawArgs));
			});
}
