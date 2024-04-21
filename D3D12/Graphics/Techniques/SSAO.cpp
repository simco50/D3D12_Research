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
{
	m_pSSAOPSO = pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "PostProcessing/SSAO.hlsl", "CSMain");
	m_pSSAOBlurPSO = pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "PostProcessing/SSAOBlur.hlsl", "CSMain");
}

RGTexture* SSAO::Execute(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures)
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

	TextureDesc textureDesc = TextureDesc::Create2D(sceneTextures.pDepth->GetDesc().Width, sceneTextures.pDepth->GetDesc().Height, ResourceFormat::R8_UNORM);
	RGTexture* pRawAmbientOcclusion = graph.Create("Raw Ambient Occlusion", textureDesc);

	graph.AddPass("SSAO", RGPassFlag::Compute)
		.Read(sceneTextures.pDepth)
		.Write(pRawAmbientOcclusion)
		.Bind([=](CommandContext& context)
			{
				Texture* pTarget = pRawAmbientOcclusion->Get();

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
				context.BindResources(3, sceneTextures.pDepth->Get()->GetSRV());

				context.Dispatch(ComputeUtils::GetNumThreadGroups(pTarget->GetWidth(), 16, pTarget->GetHeight(), 16));
			});

	RGTexture* pBlurTarget = graph.Create("AO Blur", textureDesc);

	graph.AddPass("Blur SSAO - Horizonal", RGPassFlag::Compute)
		.Read({ pRawAmbientOcclusion, sceneTextures.pDepth })
		.Write(pBlurTarget)
		.Bind([=](CommandContext& context)
			{
				Texture* pAO = pRawAmbientOcclusion->Get();
				Texture* pTarget = pBlurTarget->Get();

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
					sceneTextures.pDepth->Get()->GetSRV(),
					pAO->GetSRV(),
					});

				context.Dispatch(ComputeUtils::GetNumThreadGroups(pTarget->GetWidth(), 256, pTarget->GetHeight(), 1));
			});

	RGTexture* pAmbientOcclusion = graph.Create("Ambient Occlusion", textureDesc);

	graph.AddPass("Blur SSAO - Vertical", RGPassFlag::Compute)
		.Read({ pBlurTarget, sceneTextures.pDepth })
		.Write(pAmbientOcclusion)
		.Bind([=](CommandContext& context)
			{
				Texture* pTarget = pAmbientOcclusion->Get();
				Texture* pBlurSource = pBlurTarget->Get();

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
					sceneTextures.pDepth->Get()->GetSRV(),
					pBlurSource->GetSRV(),
					});

				context.Dispatch(ComputeUtils::GetNumThreadGroups(pBlurSource->GetWidth(), 1, pBlurSource->GetHeight(), 256));
			});

	return pAmbientOcclusion;
}
