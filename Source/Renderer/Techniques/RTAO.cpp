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
	RGTexture* pRayTraceTarget = graph.Create("RTAO.RaytraceTarget", aoDesc);

	graph.AddPass("TraceRays", RGPassFlag::Compute)
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
					RWTextureView Output;
					TextureView Depth;
				} parameters{};

				parameters.Power = g_AoPower;
				parameters.Radius = g_AoRadius;
				parameters.Samples = g_AoSamples;
				parameters.Output = pTarget->GetUAV();
				parameters.Depth = resources.GetSRV(pDepth);

				ShaderBindingTable bindingTable(m_pTraceRaysSO);
				bindingTable.BindRayGenShader("RayGen");
				bindingTable.BindMissShader("OcclusionMS", {});

				Renderer::BindViewUniforms(context, *pView);
				context.BindRootSRV(BindingSlot::PerInstance, parameters);

				context.DispatchRays(bindingTable, pTarget->GetWidth(), pTarget->GetHeight());
			});

	RGTexture* pDenoiseTarget = graph.Create("RTAO.DenoiseTarget", aoDesc);
	RGTexture* pAOHistory = graph.TryImport(m_pHistory, GraphicsCommon::GetDefaultTexture(DefaultTexture::Black2D));

	graph.AddPass("Denoise", RGPassFlag::Compute)
		.Read({ pRayTraceTarget, pVelocity, pDepth, pAOHistory })
		.Write(pDenoiseTarget)
		.Bind([=](CommandContext& context, const RGResources& resources)
			{
				Texture* pTarget = resources.Get(pDenoiseTarget);
				context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
				context.SetPipelineState(m_pDenoisePSO);

				struct
				{
					RWTextureView Output;
					TextureView Depth;
					TextureView History;
					TextureView AO;
					TextureView Velocity;
				} params;

				params.Output = pTarget->GetUAV();
				params.Depth = resources.GetSRV(pDepth);
				params.History = resources.GetSRV(pAOHistory);
				params.AO = resources.GetSRV(pRayTraceTarget);
				params.Velocity = resources.GetSRV(pVelocity);

				context.BindRootSRV(BindingSlot::PerInstance, params);
				Renderer::BindViewUniforms(context, *pView);
				context.Dispatch(ComputeUtils::GetNumThreadGroups(pTarget->GetWidth(), 8, pTarget->GetHeight(), 8));
			});

	graph.Export(pDenoiseTarget, &m_pHistory, TextureFlag::ShaderResource);

	RGTexture* pBlurTarget1 = graph.Create("RTAO.BlurTarget", aoDesc);

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
					TextureView SceneDepth;
					TextureView Input;
					RWTextureView Output;
				} shaderParameters;

				shaderParameters.Horizontal = 1;
				shaderParameters.DimensionsInv = Vector2(1.0f / pTarget->GetWidth(), 1.0f / pTarget->GetHeight());
				shaderParameters.SceneDepth = resources.GetSRV(pDepth);
				shaderParameters.Input = resources.GetSRV(pDenoiseTarget);
				shaderParameters.Output = pTarget->GetUAV();

				context.BindRootSRV(BindingSlot::PerInstance, shaderParameters);

				context.Dispatch(ComputeUtils::GetNumThreadGroups(pTarget->GetWidth(), 256, pTarget->GetHeight(), 1));
			});

	RGTexture* pFinalAOTarget = graph.Create("RTAO.Output", aoDesc);

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
					TextureView SceneDepth;
					TextureView Input;
					RWTextureView Output;
				} shaderParameters;

				shaderParameters.DimensionsInv = Vector2(1.0f / pTarget->GetWidth(), 1.0f / pTarget->GetHeight());
				shaderParameters.Horizontal = 0;
				shaderParameters.SceneDepth = resources.GetSRV(pDepth);
				shaderParameters.Input = resources.GetSRV(pRayTraceTarget);
				shaderParameters.Output = pTarget->GetUAV();

				context.BindRootSRV(BindingSlot::PerInstance, shaderParameters);
				
				context.Dispatch(ComputeUtils::GetNumThreadGroups(pTarget->GetWidth(), 1, pTarget->GetHeight(), 256));
			});
	return pFinalAOTarget;
}
