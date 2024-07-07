#include "stdafx.h"
#include "SSAO.h"
#include "RHI/PipelineState.h"
#include "RHI/RootSignature.h"
#include "RHI/Device.h"
#include "RHI/CommandContext.h"
#include "RHI/Texture.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/SceneView.h"

SSAO::SSAO(GraphicsDevice* pDevice)
{
	m_pSSAOPSO = pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "PostProcessing/SSAO.hlsl", "CSMain");
	m_pSSAOBlurPSO = pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "PostProcessing/SSAOBlur.hlsl", "CSMain");
}

RGTexture* SSAO::Execute(RGGraph& graph, const SceneView* pView, RGTexture* pDepth)
{
	static float g_AoPower = 1.2f;
	static float g_AoThreshold = 0.0025f;
	static float g_AoRadius = 0.3f;
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

	TextureDesc textureDesc = TextureDesc::Create2D(pDepth->GetDesc().Width, pDepth->GetDesc().Height, ResourceFormat::R8_UNORM);
	RGTexture* pRawAmbientOcclusion = graph.Create("Raw Ambient Occlusion", textureDesc);

	graph.AddPass("SSAO", RGPassFlag::Compute)
		.Read(pDepth)
		.Write(pRawAmbientOcclusion)
		.Bind([=](CommandContext& context, const RGResources& resources)
			{
				Texture* pTarget = resources.Get(pRawAmbientOcclusion);

				context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
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

				context.BindRootCBV(0, shaderParameters);
				context.BindRootCBV(1, Renderer::GetViewUniforms(pView, pTarget));
				context.BindResources(2, pTarget->GetUAV());
				context.BindResources(3, resources.GetSRV(pDepth));

				context.Dispatch(ComputeUtils::GetNumThreadGroups(pTarget->GetWidth(), 16, pTarget->GetHeight(), 16));
			});

	RGTexture* pBlurTarget = graph.Create("AO Blur", textureDesc);

	graph.AddPass("Blur SSAO - Horizonal", RGPassFlag::Compute)
		.Read({ pRawAmbientOcclusion, pDepth })
		.Write(pBlurTarget)
		.Bind([=](CommandContext& context, const RGResources& resources)
			{
				Texture* pAO = resources.Get(pRawAmbientOcclusion);
				Texture* pTarget = resources.Get(pBlurTarget);

				context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
				context.SetPipelineState(m_pSSAOBlurPSO);

				struct
				{
					Vector2 DimensionsInv;
					uint32 Horizontal;
				} shaderParameters;

				shaderParameters.Horizontal = 1;
				shaderParameters.DimensionsInv = Vector2(1.0f / pAO->GetWidth(), 1.0f / pAO->GetHeight());

				context.BindRootCBV(0, shaderParameters);
				context.BindResources(2, pTarget->GetUAV());
				context.BindResources(3, {
					resources.GetSRV(pDepth),
					pAO->GetSRV(),
					});

				context.Dispatch(ComputeUtils::GetNumThreadGroups(pTarget->GetWidth(), 256, pTarget->GetHeight(), 1));
			});

	RGTexture* pAmbientOcclusion = graph.Create("Ambient Occlusion", textureDesc);

	graph.AddPass("Blur SSAO - Vertical", RGPassFlag::Compute)
		.Read({ pBlurTarget, pDepth })
		.Write(pAmbientOcclusion)
		.Bind([=](CommandContext& context, const RGResources& resources)
			{
				Texture* pTarget = resources.Get(pAmbientOcclusion);
				Texture* pBlurSource = resources.Get(pBlurTarget);

				context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
				context.SetPipelineState(m_pSSAOBlurPSO);

				struct
				{
					Vector2 DimensionsInv;
					uint32 Horizontal;
				} shaderParameters;

				shaderParameters.DimensionsInv = Vector2(1.0f / pTarget->GetWidth(), 1.0f / pTarget->GetHeight());
				shaderParameters.Horizontal = 0;

				context.BindRootCBV(0, shaderParameters);
				context.BindResources(2, pTarget->GetUAV());
				context.BindResources(3, {
					resources.GetSRV(pDepth),
					pBlurSource->GetSRV(),
					});

				context.Dispatch(ComputeUtils::GetNumThreadGroups(pBlurSource->GetWidth(), 1, pBlurSource->GetHeight(), 256));
			});

	return pAmbientOcclusion;
}
