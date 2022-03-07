#include "stdafx.h"
#include "RTAO.h"
#include "Graphics/Core/Shader.h"
#include "Graphics/Core/RootSignature.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/Core/CommandContext.h"
#include "Graphics/Core/Texture.h"
#include "Graphics/Core/ShaderBindingTable.h"
#include "Graphics/Core/StateObject.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/SceneView.h"

RTAO::RTAO(GraphicsDevice* pDevice)
{
	if (pDevice->GetCapabilities().SupportsRaytracing())
	{
		SetupPipelines(pDevice);
	}
}

void RTAO::Execute(RGGraph& graph, const SceneView& sceneData, Texture* pTarget, Texture* pDepth)
{
	static float g_AoPower = 0.7f;
	static float g_AoRadius = 0.7f;
	static int32 g_AoSamples = 8;

	if (ImGui::Begin("Parameters"))
	{
		if (ImGui::CollapsingHeader("Ambient Occlusion"))
		{
			ImGui::SliderFloat("Power", &g_AoPower, 0, 10);
			ImGui::SliderFloat("Radius", &g_AoRadius, 0.1f, 5.0f);
			ImGui::SliderInt("Samples", &g_AoSamples, 1, 64);
		}
	}
	ImGui::End();

	RGPassBuilder rt = graph.AddPass("RTAO");
	rt.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
		{
			context.InsertResourceBarrier(pDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(pTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			context.SetComputeRootSignature(m_pGlobalRS);
			context.SetPipelineState(m_pRtSO);

			struct
			{
				float Power;
				float Radius;
				uint32 Samples;
			} parameters{};

			parameters.Power = g_AoPower;
			parameters.Radius = g_AoRadius;
			parameters.Samples = g_AoSamples;

			ShaderBindingTable bindingTable(m_pRtSO);
			bindingTable.BindRayGenShader("RayGen");
			bindingTable.BindMissShader("Miss", {});

			context.SetRootCBV(0, parameters);
			context.SetRootCBV(1, GetViewUniforms(sceneData, pTarget));
			context.BindResource(2, 0, pTarget->GetUAV());
			context.BindResource(3, 0, pDepth->GetSRV());

			context.DispatchRays(bindingTable, pTarget->GetWidth(), pTarget->GetHeight());
		});
}

void RTAO::SetupPipelines(GraphicsDevice* pDevice)
{
	ShaderLibrary* pShaderLibrary = pDevice->GetLibrary("RTAO.hlsl");
	m_pGlobalRS = new RootSignature(pDevice);
	m_pGlobalRS->FinalizeFromShader("Global", pShaderLibrary);

	StateObjectInitializer stateDesc;
	stateDesc.AddLibrary(pShaderLibrary, { "RayGen", "Miss" });
	stateDesc.Name = "RT AO";
	stateDesc.MaxPayloadSize = sizeof(float);
	stateDesc.MaxAttributeSize = 2 * sizeof(float);
	stateDesc.pGlobalRootSignature = m_pGlobalRS;
	stateDesc.RayGenShader = "RayGen";
	stateDesc.AddMissShader("Miss");
	m_pRtSO = pDevice->CreateStateObject(stateDesc);
}
