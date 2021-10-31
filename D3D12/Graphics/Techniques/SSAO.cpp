#include "stdafx.h"
#include "SSAO.h"
#include "Graphics/Core/Shader.h"
#include "Graphics/Core/PipelineState.h"
#include "Graphics/Core/RootSignature.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/Core/CommandContext.h"
#include "Graphics/Core/Texture.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Scene/Camera.h"
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

void SSAO::Execute(RGGraph& graph, Texture* pTarget, const SceneView& sceneData)
{
	static float g_AoPower = 3;
	static float g_AoThreshold = 0.0025f;
	static float g_AoRadius = 0.5f;
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
	ssao.Bind([=](CommandContext& renderContext, const RGPassResources& /*passResources*/)
		{
			renderContext.InsertResourceBarrier(sceneData.pResolvedDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			renderContext.InsertResourceBarrier(pTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			renderContext.SetComputeRootSignature(m_pSSAORS.get());
			renderContext.SetPipelineState(m_pSSAOPSO);

			struct ShaderParameters
			{
				Matrix ProjectionInverse;
				Matrix ViewInverse;
				Matrix Projection;
				Matrix View;
				IntVector2 Dimensions;
				float Near;
				float Far;
				float Power;
				float Radius;
				float Threshold;
				int Samples;
				int FrameIndex;
			} shaderParameters{};

			shaderParameters.ProjectionInverse = sceneData.pCamera->GetProjectionInverse();
			shaderParameters.ViewInverse = sceneData.pCamera->GetViewInverse();
			shaderParameters.Projection = sceneData.pCamera->GetProjection();
			shaderParameters.View = sceneData.pCamera->GetView();
			shaderParameters.Dimensions.x = pTarget->GetWidth();
			shaderParameters.Dimensions.y = pTarget->GetHeight();
			shaderParameters.Near = sceneData.pCamera->GetNear();
			shaderParameters.Far = sceneData.pCamera->GetFar();
			shaderParameters.Power = g_AoPower;
			shaderParameters.Radius = g_AoRadius;
			shaderParameters.Threshold = g_AoThreshold;
			shaderParameters.Samples = g_AoSamples;
			shaderParameters.FrameIndex = sceneData.FrameIndex;

			renderContext.SetRootCBV(0, shaderParameters);
			renderContext.BindResource(1, 0, pTarget->GetUAV());
			renderContext.BindResource(2, 0, sceneData.pResolvedDepth->GetSRV());

			int dispatchGroupsX = Math::DivideAndRoundUp(pTarget->GetWidth(), 16);
			int dispatchGroupsY = Math::DivideAndRoundUp(pTarget->GetHeight(), 16);
			renderContext.Dispatch(dispatchGroupsX, dispatchGroupsY);
		});

	RGPassBuilder blur = graph.AddPass("Blur SSAO");
	blur.Bind([=](CommandContext& renderContext, const RGPassResources& /*passResources*/)
		{
			renderContext.InsertResourceBarrier(m_pAmbientOcclusionIntermediate.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			renderContext.InsertResourceBarrier(pTarget, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);

			renderContext.SetComputeRootSignature(m_pSSAOBlurRS.get());
			renderContext.SetPipelineState(m_pSSAOBlurPSO);

			struct ShaderParameters
			{
				Vector2 DimensionsInv;
				uint32 Horizontal;
				float Far;
				float Near;
			} shaderParameters;

			shaderParameters.Horizontal = 1;
			shaderParameters.DimensionsInv = Vector2(1.0f / pTarget->GetWidth(), 1.0f / pTarget->GetHeight());
			shaderParameters.Far = sceneData.pCamera->GetFar();
			shaderParameters.Near = sceneData.pCamera->GetNear();

			renderContext.SetRootCBV(0, shaderParameters);
			renderContext.BindResource(1, 0, m_pAmbientOcclusionIntermediate->GetUAV());
			renderContext.BindResource(2, 0, sceneData.pResolvedDepth->GetSRV());
			renderContext.BindResource(2, 1, pTarget->GetSRV());

			renderContext.Dispatch(Math::DivideAndRoundUp(m_pAmbientOcclusionIntermediate->GetWidth(), 256), m_pAmbientOcclusionIntermediate->GetHeight());

			renderContext.InsertResourceBarrier(m_pAmbientOcclusionIntermediate.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			renderContext.InsertResourceBarrier(pTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			renderContext.BindResource(1, 0, pTarget->GetUAV());
			renderContext.BindResource(2, 0, sceneData.pResolvedDepth->GetSRV());
			renderContext.BindResource(2, 1, m_pAmbientOcclusionIntermediate->GetSRV());

			shaderParameters.Horizontal = 0;
			renderContext.SetRootCBV(0, shaderParameters);
			renderContext.Dispatch(m_pAmbientOcclusionIntermediate->GetWidth(), Math::DivideAndRoundUp(m_pAmbientOcclusionIntermediate->GetHeight(), 256));
		});
}

void SSAO::SetupPipelines()
{
	//SSAO
	{
		Shader* pComputeShader = m_pDevice->GetShader("SSAO.hlsl", ShaderType::Compute, "CSMain");

		m_pSSAORS = std::make_unique<RootSignature>(m_pDevice);
		m_pSSAORS->FinalizeFromShader("SSAO", pComputeShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetRootSignature(m_pSSAORS->GetRootSignature());
		psoDesc.SetName("SSAO");
		m_pSSAOPSO = m_pDevice->CreatePipeline(psoDesc);
	}

	//SSAO Blur
	{
		Shader* pComputeShader = m_pDevice->GetShader("SSAOBlur.hlsl", ShaderType::Compute, "CSMain");

		m_pSSAOBlurRS = std::make_unique<RootSignature>(m_pDevice);
		m_pSSAOBlurRS->FinalizeFromShader("SSAO Blur", pComputeShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetRootSignature(m_pSSAOBlurRS->GetRootSignature());
		psoDesc.SetName("SSAO Blur");
		m_pSSAOBlurPSO = m_pDevice->CreatePipeline(psoDesc);
	}
}
