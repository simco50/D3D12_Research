#include "stdafx.h"
#include "SSAO.h"
#include "Graphics/RHI/PipelineState.h"
#include "Graphics/RHI/RootSignature.h"
#include "Graphics/RHI/Graphics.h"
#include "Graphics/RHI/CommandContext.h"
#include "Graphics/RHI/Texture.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/SceneView.h"

SSAO::SSAO(GraphicsDevice* pDevice)
	: m_pDevice(pDevice)
{
	SetupPipelines();
}

void SSAO::Execute(RGGraph& graph, const SceneView& view, SceneTextures& sceneTextures)
{
	static float g_AoPower = 7;
	static float g_AoThreshold = 0.0025f;
	static float g_AoRadius = 0.03f;
	static int g_AoSamples = 16;

	if (ImGui::Begin("Parameters"))
	{
		if (ImGui::CollapsingHeader("Ambient Occlusion"))
		{
			ImGui::SliderFloat("Power", &g_AoPower, 0, 10);
			ImGui::SliderFloat("Threshold", &g_AoThreshold, 0.0001f, 0.01f);
			ImGui::SliderFloat("Radius", &g_AoRadius, 0, 2);
			ImGui::SliderInt("Samples", &g_AoSamples, 1, 64);
		}
	}
	ImGui::End();

	RG_GRAPH_SCOPE("Ambient Occlusion", graph);

	graph.AddPass("SSAO", RGPassFlag::Compute)
		.Read(sceneTextures.Depth)
		.Write(&sceneTextures.AmbientOcclusion)
		.Bind([=](CommandContext& context, const RGPassResources& resources)
			{
				Texture* pTarget = resources.Get<Texture>(sceneTextures.AmbientOcclusion);

				context.SetComputeRootSignature(m_pSSAORS);
				context.SetPipelineState(m_pSSAOPSO);

				struct
				{
					float Power;
					float Radius;
					float Threshold;
					int Samples;
				} shaderParameters{};

				shaderParameters.Power = g_AoPower;
				shaderParameters.Radius = g_AoRadius;
				shaderParameters.Threshold = g_AoThreshold;
				shaderParameters.Samples = g_AoSamples;

				context.SetRootConstants(0, shaderParameters);
				context.SetRootCBV(1, Renderer::GetViewUniforms(view, pTarget));
				context.BindResources(2, pTarget->GetUAV());
				context.BindResources(3, resources.Get<Texture>(sceneTextures.Depth)->GetSRV());

				context.Dispatch(ComputeUtils::GetNumThreadGroups(pTarget->GetWidth(), 16, pTarget->GetHeight(), 16));
			});

	RGResourceHandle aoIntermediate = graph.CreateTexture("Intermediate AO", graph.GetDesc(sceneTextures.AmbientOcclusion));

	graph.AddPass("Blur SSAO - Horizonal", RGPassFlag::Compute)
		.Read({ sceneTextures.AmbientOcclusion, sceneTextures.Depth })
		.Write(&aoIntermediate)
		.Bind([=](CommandContext& context, const RGPassResources& resources)
			{
				Texture* pTarget = resources.Get<Texture>(sceneTextures.AmbientOcclusion);
				Texture* pBlurTarget = resources.Get<Texture>(aoIntermediate);

				context.SetComputeRootSignature(m_pSSAORS);
				context.SetPipelineState(m_pSSAOBlurPSO);

				struct
				{
					Vector2 DimensionsInv;
					uint32 Horizontal;
				} shaderParameters;

				shaderParameters.Horizontal = 1;
				shaderParameters.DimensionsInv = Vector2(1.0f / pTarget->GetWidth(), 1.0f / pTarget->GetHeight());

				context.SetRootConstants(0, shaderParameters);
				context.SetRootCBV(1, Renderer::GetViewUniforms(view, pTarget));
				context.BindResources(2, pBlurTarget->GetUAV());
				context.BindResources(3, {
					resources.Get<Texture>(sceneTextures.Depth)->GetSRV(),
					pTarget->GetSRV(),
					});

				context.Dispatch(ComputeUtils::GetNumThreadGroups(pBlurTarget->GetWidth(), 256, pBlurTarget->GetHeight(), 1));
			});

	graph.AddPass("Blur SSAO - Vertical", RGPassFlag::Compute)
		.Read({ aoIntermediate, sceneTextures.Depth })
		.Write(&sceneTextures.AmbientOcclusion)
		.Bind([=](CommandContext& context, const RGPassResources& resources)
			{
				Texture* pTarget = resources.Get<Texture>(sceneTextures.AmbientOcclusion);
				Texture* pBlurSource = resources.Get<Texture>(aoIntermediate);

				context.SetComputeRootSignature(m_pSSAORS);
				context.SetPipelineState(m_pSSAOBlurPSO);

				struct
				{
					Vector2 DimensionsInv;
					uint32 Horizontal;
				} shaderParameters;

				shaderParameters.DimensionsInv = Vector2(1.0f / pTarget->GetWidth(), 1.0f / pTarget->GetHeight());
				shaderParameters.Horizontal = 0;

				context.BindResources(2, pTarget->GetUAV());
				context.BindResources(3, {
					resources.Get<Texture>(sceneTextures.Depth)->GetSRV(),
					pBlurSource->GetSRV(),
					});

				context.SetRootConstants(0, shaderParameters);
				context.Dispatch(ComputeUtils::GetNumThreadGroups(pBlurSource->GetWidth(), 1, pBlurSource->GetHeight(), 256));
			});
}

void SSAO::SetupPipelines()
{
	m_pSSAORS = new RootSignature(m_pDevice);
	m_pSSAORS->AddRootConstants(0, 4);
	m_pSSAORS->AddConstantBufferView(100);
	m_pSSAORS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2);
	m_pSSAORS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2);
	m_pSSAORS->Finalize("SSAO");

	m_pSSAOPSO = m_pDevice->CreateComputePipeline(m_pSSAORS, "SSAO.hlsl", "CSMain");
	m_pSSAOBlurPSO = m_pDevice->CreateComputePipeline(m_pSSAORS, "SSAOBlur.hlsl", "CSMain");
}
