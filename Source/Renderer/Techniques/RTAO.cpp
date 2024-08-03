#include "stdafx.h"
#include "RTAO.h"
#include "RHI/RootSignature.h"
#include "RHI/Device.h"
#include "RHI/CommandContext.h"
#include "RHI/Texture.h"
#include "RHI/ShaderBindingTable.h"
#include "RHI/StateObject.h"
#include "RHI/PipelineState.h"
#include "Renderer/Renderer.h"
#include "RenderGraph/RenderGraph.h"

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

RGTexture* RTAO::Execute(RGGraph& graph, const RenderView* pView, RGTexture* pDepth, RGTexture* pVelocity)
{
	static float g_AoPower = 1.0f;
	static float g_AoRadius = 2.0f;
	static int32 g_AoSamples = 1;

	if (ImGui::Begin("Settings"))
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

	TextureDesc aoDesc = TextureDesc::Create2D(pDepth->GetDesc().Width, pDepth->GetDesc().Height, ResourceFormat::R8_UNORM);
	RGTexture* pRayTraceTarget = graph.Create("Raytrace Target", aoDesc);

	graph.AddPass("Trace Rays", RGPassFlag::Compute)
		.Read(pDepth)
		.Write(pRayTraceTarget)
		.Bind([=](CommandContext& context, const RGResources& resources)
			{
				Texture* pTarget = resources.Get(pRayTraceTarget);
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

				Renderer::BindViewUniforms(context, *pView);
				context.BindRootCBV(BindingSlot::PerInstance, parameters);
				context.BindResources(BindingSlot::UAV, pTarget->GetUAV());
				context.BindResources(BindingSlot::SRV, {
					resources.GetSRV(pDepth),
					});

				context.DispatchRays(bindingTable, pTarget->GetWidth(), pTarget->GetHeight());
			});

	RGTexture* pDenoiseTarget = graph.Create("Denoise Target", aoDesc);
	RGTexture* pAOHistory = graph.TryImport(m_pHistory, GraphicsCommon::GetDefaultTexture(DefaultTexture::Black2D));

	graph.AddPass("Denoise", RGPassFlag::Compute)
		.Read({ pRayTraceTarget, pVelocity, pDepth, pAOHistory })
		.Write(pDenoiseTarget)
		.Bind([=](CommandContext& context, const RGResources& resources)
			{
				Texture* pTarget = resources.Get(pDenoiseTarget);
				context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
				context.SetPipelineState(m_pDenoisePSO);

				Renderer::BindViewUniforms(context, *pView);
				context.BindResources(BindingSlot::UAV, pTarget->GetUAV());
				context.BindResources(BindingSlot::SRV, {
					resources.GetSRV(pDepth),
					resources.GetSRV(pAOHistory),
					resources.GetSRV(pRayTraceTarget),
					resources.GetSRV(pVelocity),
					});
				context.Dispatch(ComputeUtils::GetNumThreadGroups(pTarget->GetWidth(), 8, pTarget->GetHeight(), 8));
			});

	graph.Export(pDenoiseTarget, &m_pHistory, TextureFlag::ShaderResource);

	RGTexture* pBlurTarget1 = graph.Create("Bilateral Blur Target", aoDesc);

	graph.AddPass("Blur AO - Horizontal", RGPassFlag::Compute)
		.Read({ pDenoiseTarget, pDepth })
		.Write(pBlurTarget1)
		.Bind([=](CommandContext& context, const RGResources& resources)
			{
				Texture* pTarget = resources.Get(pBlurTarget1);
				context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
				context.SetPipelineState(m_pBilateralBlurPSO);

				struct
				{
					Vector2 DimensionsInv;
					uint32 Horizontal;
				} shaderParameters;

				shaderParameters.Horizontal = 1;
				shaderParameters.DimensionsInv = Vector2(1.0f / pTarget->GetWidth(), 1.0f / pTarget->GetHeight());

				context.BindRootCBV(BindingSlot::PerInstance, shaderParameters);
				context.BindResources(BindingSlot::UAV, pTarget->GetUAV());
				context.BindResources(BindingSlot::SRV, {
					resources.GetSRV(pDepth),
					resources.GetSRV(pDenoiseTarget)
					});

				context.Dispatch(ComputeUtils::GetNumThreadGroups(pTarget->GetWidth(), 256, pTarget->GetHeight(), 1));
			});

	RGTexture* pFinalAOTarget = graph.Create("Ambient Occlusion", aoDesc);

	graph.AddPass("Blur AO - Vertical", RGPassFlag::Compute)
		.Read({ pRayTraceTarget, pDepth })
		.Write(pFinalAOTarget)
		.Bind([=](CommandContext& context, const RGResources& resources)
			{
				Texture* pTarget = resources.Get(pFinalAOTarget);

				context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
				context.SetPipelineState(m_pBilateralBlurPSO);

				struct
				{
					Vector2 DimensionsInv;
					uint32 Horizontal;
				} shaderParameters;

				shaderParameters.DimensionsInv = Vector2(1.0f / pTarget->GetWidth(), 1.0f / pTarget->GetHeight());
				shaderParameters.Horizontal = 0;

				context.BindRootCBV(BindingSlot::PerInstance, shaderParameters);
				context.BindResources(BindingSlot::UAV, pTarget->GetUAV());
				context.BindResources(BindingSlot::SRV, {
					resources.GetSRV(pDepth),
					resources.GetSRV(pRayTraceTarget)
					});

				context.Dispatch(ComputeUtils::GetNumThreadGroups(pTarget->GetWidth(), 1, pTarget->GetHeight(), 256));
			});
	return pFinalAOTarget;
}
