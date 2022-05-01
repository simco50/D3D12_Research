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

void SSAO::OnResize(int windowWidth, int windowHeight)
{
	m_pAmbientOcclusionIntermediate = m_pDevice->CreateTexture(TextureDesc::Create2D(Math::DivideAndRoundUp(windowWidth, 2), Math::DivideAndRoundUp(windowHeight, 2), DXGI_FORMAT_R8_UNORM, TextureFlag::UnorderedAccess | TextureFlag::ShaderResource), "Intermediate AO");
}

void SSAO::Execute(RGGraph& graph, const SceneView& view, const SceneTextures& sceneTextures)
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

	RefCountPtr<Texture> pTarget = sceneTextures.pAmbientOcclusion;

	graph.AddPass("SSAO")
		.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
		{
			context.InsertResourceBarrier(sceneTextures.pDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
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
			context.SetRootCBV(1, Renderer::GetViewUniforms(view, pTarget));
			context.BindResources(2, pTarget->GetUAV());
			context.BindResources(3, sceneTextures.pDepth->GetSRV());

			context.Dispatch(ComputeUtils::GetNumThreadGroups(pTarget->GetWidth(), 16, pTarget->GetHeight(), 16));
		});

	graph.AddPass("Blur SSAO")
		.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
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
			context.SetRootCBV(1, Renderer::GetViewUniforms(view, pTarget));
			context.BindResources(2, m_pAmbientOcclusionIntermediate->GetUAV());
			context.BindResources(3, {
				sceneTextures.pDepth->GetSRV(),
				pTarget->GetSRV(),
			});

			context.Dispatch(ComputeUtils::GetNumThreadGroups(m_pAmbientOcclusionIntermediate->GetWidth(), 256, m_pAmbientOcclusionIntermediate->GetHeight(), 1));

			context.InsertResourceBarrier(m_pAmbientOcclusionIntermediate, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(pTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			context.BindResources(2, pTarget->GetUAV());
			context.BindResources(3, {
				sceneTextures.pDepth->GetSRV(),
				m_pAmbientOcclusionIntermediate->GetSRV(),
			});

			shaderParameters.Horizontal = 0;
			context.SetRootConstants(0, shaderParameters);
			context.Dispatch(ComputeUtils::GetNumThreadGroups(m_pAmbientOcclusionIntermediate->GetWidth(), 1, m_pAmbientOcclusionIntermediate->GetHeight(), 256));
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
