#include "stdafx.h"
#include "RTAO.h"
#include "Graphics/Core/Shader.h"
#include "Graphics/Core/RootSignature.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/Core/CommandContext.h"
#include "Graphics/Core/Texture.h"
#include "Graphics/Core/ShaderBindingTable.h"
#include "Graphics/Core/StateObject.h"
#include "Graphics/Core/PipelineState.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/SceneView.h"

RTAO::RTAO(GraphicsDevice* pDevice)
	: m_pDevice(pDevice)
{
	if (pDevice->GetCapabilities().SupportsRaytracing())
	{
		SetupPipelines(pDevice);
	}
}

void RTAO::Execute(RGGraph& graph, const SceneView& sceneData, SceneTextures& sceneTextures)
{
	TextureDesc aoDesc = sceneTextures.pAmbientOcclusion->GetDesc();
	if (!m_Targets[0] || m_Targets[0]->GetDesc() != aoDesc)
	{
		m_Targets[0] = m_pDevice->CreateTexture(aoDesc, "AO Target 0");
		m_Targets[1] = m_pDevice->CreateTexture(aoDesc, "AO Target 1");
		m_pHistory = m_pDevice->CreateTexture(aoDesc, "AO History");
	}

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

	Texture* pRayTraceTarget = m_Targets[0];
	Texture* pDenoiseTarget = m_Targets[1];

	RGPassBuilder rt = graph.AddPass("Trace Rays");
	rt.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
		{
			context.InsertResourceBarrier(sceneTextures.pDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(pRayTraceTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			context.SetComputeRootSignature(m_pCommonRS);
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

			context.SetRootConstants(0, parameters);
			context.SetRootCBV(1, GetViewUniforms(sceneData, pRayTraceTarget));
			context.BindResource(2, 0, pRayTraceTarget->GetUAV());
			context.BindResources(3, {
				sceneTextures.pDepth->GetSRV()
				});

			context.DispatchRays(bindingTable, pRayTraceTarget->GetWidth(), pRayTraceTarget->GetHeight());
		});

	RGPassBuilder denoiseAO = graph.AddPass("Denoise");
	denoiseAO.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
		{
			context.InsertResourceBarrier(pRayTraceTarget, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pHistory, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(pDenoiseTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			context.SetComputeRootSignature(m_pCommonRS);
			context.SetPipelineState(m_pDenoisePSO);

			//context.SetRootCBV(0, parameters);
			context.SetRootCBV(1, GetViewUniforms(sceneData, pDenoiseTarget));
			context.BindResource(2, 0, pDenoiseTarget->GetUAV());
			context.BindResources(3, {
				sceneTextures.pDepth->GetSRV(),
				m_pHistory->GetSRV(),
				pRayTraceTarget->GetSRV(),
				sceneTextures.pVelocity->GetSRV(),
				});

			context.Dispatch(ComputeUtils::GetNumThreadGroups(pDenoiseTarget->GetWidth(), 8, pDenoiseTarget->GetHeight(), 8));

			context.InsertResourceBarrier(pDenoiseTarget, D3D12_RESOURCE_STATE_COPY_SOURCE);
			context.InsertResourceBarrier(m_pHistory, D3D12_RESOURCE_STATE_COPY_DEST);
			context.CopyTexture(pDenoiseTarget, m_pHistory);
		});

	RGPassBuilder blur = graph.AddPass("Blur AO");
	blur.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
		{
			Texture* pSource = pDenoiseTarget;
			Texture* pTarget = pRayTraceTarget;

			context.InsertResourceBarrier(pDenoiseTarget, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
			context.InsertResourceBarrier(pTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			context.SetComputeRootSignature(m_pCommonRS);
			context.SetPipelineState(m_pBilateralBlurPSO);

			struct
			{
				Vector2 DimensionsInv;
				uint32 Horizontal;
			} shaderParameters;

			shaderParameters.Horizontal = 1;
			shaderParameters.DimensionsInv = Vector2(1.0f / pTarget->GetWidth(), 1.0f / pTarget->GetHeight());

			context.SetRootConstants(0, shaderParameters);
			context.SetRootCBV(1, GetViewUniforms(sceneData, pTarget));
			context.BindResource(2, 0, pTarget->GetUAV());
			context.BindResource(3, 0, sceneTextures.pDepth->GetSRV());
			context.BindResource(3, 1, pSource->GetSRV());

			context.Dispatch(Math::DivideAndRoundUp(pTarget->GetWidth(), 256), pTarget->GetHeight());

			pTarget = sceneTextures.pAmbientOcclusion;
			pSource = pRayTraceTarget;

			context.InsertResourceBarrier(pSource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(pTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			context.BindResource(2, 0, pTarget->GetUAV());
			context.BindResource(3, 0, sceneTextures.pDepth->GetSRV());
			context.BindResource(3, 1, pSource->GetSRV());

			shaderParameters.Horizontal = 0;
			context.SetRootConstants(0, shaderParameters);
			context.Dispatch(pTarget->GetWidth(), Math::DivideAndRoundUp(pTarget->GetHeight(), 256));
		});
}

void RTAO::SetupPipelines(GraphicsDevice* pDevice)
{
	m_pCommonRS = new RootSignature(pDevice);
	m_pCommonRS->AddRootConstants(0, 4);
	m_pCommonRS->AddConstantBufferView(100);
	m_pCommonRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1);
	m_pCommonRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4);
	m_pCommonRS->Finalize("Global");

	StateObjectInitializer stateDesc;
	stateDesc.AddLibrary("RTAOTraceRays.hlsl");
	stateDesc.AddLibrary("SharedRaytracingLib.hlsl", { "OcclusionMS" });
	stateDesc.Name = "RT AO";
	stateDesc.MaxPayloadSize = sizeof(float);
	stateDesc.pGlobalRootSignature = m_pCommonRS;
	stateDesc.RayGenShader = "RayGen";
	stateDesc.AddMissShader("OcclusionMS");
	m_pTraceRaysSO = pDevice->CreateStateObject(stateDesc);

	m_pDenoisePSO = pDevice->CreateComputePipeline(m_pCommonRS, "RTAODenoise.hlsl", "DenoiseCS");
	m_pBilateralBlurPSO = m_pDevice->CreateComputePipeline(m_pCommonRS, "SSAOBlur.hlsl", "CSMain");
}
