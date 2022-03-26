#include "stdafx.h"
#include "SSAO.h"
#include "Graphics/Core/PipelineState.h"
#include "Graphics/Core/RootSignature.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/Core/CommandContext.h"
#include "Graphics/Core/Texture.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/SceneView.h"

SSAO::SSAO(GraphicsDevice* pDevice)
	: m_pDevice(pDevice)
{
	SetupPipelines();
}

void SSAO::OnResize(int windowWidth, int windowHeight)
{
	m_pAmbientOcclusionIntermediate = m_pDevice->CreateTexture(TextureDesc::Create2D(Math::DivideAndRoundUp(windowWidth, 2), Math::DivideAndRoundUp(windowHeight, 2), DXGI_FORMAT_R8_UNORM, TextureFlag::UnorderedAccess | TextureFlag::ShaderResource), "Intermediate AO");
}

void SSAO::Execute(RGGraph& graph, const SceneView& sceneData, Texture* pTarget, Texture* pDepth)
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

	RGPassBuilder ssao = graph.AddPass("SSAO");
	ssao.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
		{
			context.InsertResourceBarrier(pDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(pTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

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
			context.SetRootCBV(1, GetViewUniforms(sceneData, pTarget));
			context.BindResource(2, 0, pTarget->GetUAV());
			context.BindResource(3, 0, pDepth->GetSRV());

			int dispatchGroupsX = Math::DivideAndRoundUp(pTarget->GetWidth(), 16);
			int dispatchGroupsY = Math::DivideAndRoundUp(pTarget->GetHeight(), 16);
			context.Dispatch(dispatchGroupsX, dispatchGroupsY);
		});

	RGPassBuilder blur = graph.AddPass("Blur SSAO");
	blur.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
		{
			context.InsertResourceBarrier(m_pAmbientOcclusionIntermediate, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			context.InsertResourceBarrier(pTarget, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);

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
			context.SetRootCBV(1, GetViewUniforms(sceneData, pTarget));
			context.BindResource(2, 0, m_pAmbientOcclusionIntermediate->GetUAV());
			context.BindResources(3, {
				pDepth->GetSRV(),
				pTarget->GetSRV(),
			});

			context.Dispatch(Math::DivideAndRoundUp(m_pAmbientOcclusionIntermediate->GetWidth(), 256), m_pAmbientOcclusionIntermediate->GetHeight());

			context.InsertResourceBarrier(m_pAmbientOcclusionIntermediate, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(pTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			context.BindResource(2, 0, pTarget->GetUAV());
			context.BindResources(3, {
				pDepth->GetSRV(),
				m_pAmbientOcclusionIntermediate->GetSRV(),
			});

			shaderParameters.Horizontal = 0;
			context.SetRootConstants(0, shaderParameters);
			context.Dispatch(m_pAmbientOcclusionIntermediate->GetWidth(), Math::DivideAndRoundUp(m_pAmbientOcclusionIntermediate->GetHeight(), 256));
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
