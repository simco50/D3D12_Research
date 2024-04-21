#include "stdafx.h"
#include "RTAO.h"
#include "Graphics/RHI/RootSignature.h"
#include "Graphics/RHI/Graphics.h"
#include "Graphics/RHI/CommandContext.h"
#include "Graphics/RHI/Texture.h"
#include "Graphics/RHI/ShaderBindingTable.h"
#include "Graphics/RHI/StateObject.h"
#include "Graphics/RHI/PipelineState.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/SceneView.h"

RTAO::RTAO(GraphicsDevice* pDevice)
{
	if (pDevice->GetCapabilities().SupportsRaytracing())
	{
		StateObjectInitializer stateDesc;
		stateDesc.AddLibrary("RayTracing/RTAOTraceRays.hlsl");
		stateDesc.AddLibrary("RayTracing/SharedRaytracingLib.hlsl", { "OcclusionMS" });
		stateDesc.Name = "RT AO";
		stateDesc.MaxPayloadSize = sizeof(float);
		stateDesc.pGlobalRootSignature = GraphicsCommon::pCommonRS;
		stateDesc.RayGenShader = "RayGen";
		stateDesc.AddMissShader("OcclusionMS");
		m_pTraceRaysSO = pDevice->CreateStateObject(stateDesc);

		m_pDenoisePSO = pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "RayTracing/RTAODenoise.hlsl", "DenoiseCS");
		m_pBilateralBlurPSO = pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "PostProcessing/SSAOBlur.hlsl", "CSMain");
	}
}

RGTexture* RTAO::Execute(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures)
{
	static float g_AoPower = 1.0f;
	static float g_AoRadius = 2.0f;
	static int32 g_AoSamples = 1;

	if (ImGui::Begin("Parameters"))
	{
		if (ImGui::CollapsingHeader("Ambient Occlusion"))
		{
			ImGui::SliderFloat("Power", &g_AoPower, 0, 1);
			ImGui::SliderFloat("Radius", &g_AoRadius, 0.1f, 4.0f);
			ImGui::SliderInt("Samples", &g_AoSamples, 1, 64);
		}
	}
	ImGui::End();

	RG_GRAPH_SCOPE("RTAO", graph);

	TextureDesc aoDesc = TextureDesc::Create2D(sceneTextures.pDepth->GetDesc().Width, sceneTextures.pDepth->GetDesc().Height, ResourceFormat::R8_UNORM);
	RGTexture* pRayTraceTarget = graph.Create("Raytrace Target", aoDesc);

	graph.AddPass("Trace Rays", RGPassFlag::Compute)
		.Read(sceneTextures.pDepth)
		.Write(pRayTraceTarget)
		.Bind([=](CommandContext& context)
			{
				Texture* pTarget = pRayTraceTarget->Get();
				context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
				context.SetPipelineState(m_pTraceRaysSO);

				struct
				{
					float Power;
					float Radius;
					uint32 Samples;
				} parameters{};

				parameters.Power = g_AoPower;
				parameters.Radius = g_AoRadius;
				parameters.Samples = g_AoSamples;

				ShaderBindingTable bindingTable(m_pTraceRaysSO);
				bindingTable.BindRayGenShader("RayGen");
				bindingTable.BindMissShader("OcclusionMS", {});

				context.BindRootCBV(0, parameters);
				context.BindRootCBV(1, Renderer::GetViewUniforms(pView, pTarget));
				context.BindResources(2, pTarget->GetUAV());
				context.BindResources(3, {
					sceneTextures.pDepth->Get()->GetSRV(),
					});

				context.DispatchRays(bindingTable, pTarget->GetWidth(), pTarget->GetHeight());
			});

	RGTexture* pDenoiseTarget = graph.Create("Denoise Target", aoDesc);
	RGTexture* pAOHistory = graph.TryImport(m_pHistory, GraphicsCommon::GetDefaultTexture(DefaultTexture::Black2D));

	graph.AddPass("Denoise", RGPassFlag::Compute)
		.Read({ pRayTraceTarget, sceneTextures.pVelocity, sceneTextures.pDepth, pAOHistory })
		.Write(pDenoiseTarget)
		.Bind([=](CommandContext& context)
			{
				Texture* pTarget = pDenoiseTarget->Get();
				context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
				context.SetPipelineState(m_pDenoisePSO);

				//context.BindRootCBV(0, parameters);
				context.BindRootCBV(1, Renderer::GetViewUniforms(pView, pTarget));
				context.BindResources(2, pTarget->GetUAV());
				context.BindResources(3, {
					sceneTextures.pDepth->Get()->GetSRV(),
					pAOHistory->Get()->GetSRV(),
					pRayTraceTarget->Get()->GetSRV(),
					sceneTextures.pVelocity->Get()->GetSRV(),
					});
				context.Dispatch(ComputeUtils::GetNumThreadGroups(pTarget->GetWidth(), 8, pTarget->GetHeight(), 8));
			});

	graph.Export(pDenoiseTarget, &m_pHistory, TextureFlag::ShaderResource);

	RGTexture* pBlurTarget1 = graph.Create("Bilateral Blur Target", aoDesc);

	graph.AddPass("Blur AO - Horizontal", RGPassFlag::Compute)
		.Read({ pDenoiseTarget, sceneTextures.pDepth })
		.Write(pBlurTarget1)
		.Bind([=](CommandContext& context)
			{
				Texture* pTarget = pBlurTarget1->Get();
				context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
				context.SetPipelineState(m_pBilateralBlurPSO);

				struct
				{
					Vector2 DimensionsInv;
					uint32 Horizontal;
				} shaderParameters;

				shaderParameters.Horizontal = 1;
				shaderParameters.DimensionsInv = Vector2(1.0f / pTarget->GetWidth(), 1.0f / pTarget->GetHeight());

				context.BindRootCBV(0, shaderParameters);
				context.BindResources(2, pTarget->GetUAV());
				context.BindResources(3, {
					sceneTextures.pDepth->Get()->GetSRV(),
					pDenoiseTarget->Get()->GetSRV()
					});

				context.Dispatch(ComputeUtils::GetNumThreadGroups(pTarget->GetWidth(), 256, pTarget->GetHeight(), 1));
			});

	RGTexture* pFinalAOTarget = graph.Create("Ambient Occlusion", aoDesc);

	graph.AddPass("Blur AO - Vertical", RGPassFlag::Compute)
		.Read({ pRayTraceTarget, sceneTextures.pDepth })
		.Write(pFinalAOTarget)
		.Bind([=](CommandContext& context)
			{
				Texture* pTarget = pFinalAOTarget->Get();

				context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
				context.SetPipelineState(m_pBilateralBlurPSO);

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
					pRayTraceTarget->Get()->GetSRV()
					});

				context.Dispatch(ComputeUtils::GetNumThreadGroups(pTarget->GetWidth(), 1, pTarget->GetHeight(), 256));
			});
	return pFinalAOTarget;
}
