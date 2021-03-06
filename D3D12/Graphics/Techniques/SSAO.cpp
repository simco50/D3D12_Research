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

SSAO::SSAO(Graphics* pGraphics)
{
	SetupResources(pGraphics);
	SetupPipelines(pGraphics);
}

void SSAO::OnSwapchainCreated(int windowWidth, int windowHeight)
{
	m_pAmbientOcclusionIntermediate->Create(TextureDesc::Create2D(Math::DivideAndRoundUp(windowWidth, 2), Math::DivideAndRoundUp(windowHeight, 2), DXGI_FORMAT_R8_UNORM, TextureFlag::UnorderedAccess | TextureFlag::ShaderResource));
}

void SSAO::Execute(RGGraph& graph, Texture* pColor, Texture* pDepth, Camera& camera)
{
	static float g_AoPower = 3;
	static float g_AoThreshold = 0.0025f;
	static float g_AoRadius = 0.5f;
	static int g_AoSamples = 16;

	ImGui::Begin("Parameters");
	ImGui::Text("Ambient Occlusion");
	ImGui::SliderFloat("Power", &g_AoPower, 0, 10);
	ImGui::SliderFloat("Threshold", &g_AoThreshold, 0.0001f, 0.01f);
	ImGui::SliderFloat("Radius", &g_AoRadius, 0, 2);
	ImGui::SliderInt("Samples", &g_AoSamples, 1, 64);
	ImGui::End();

	RG_GRAPH_SCOPE("Ambient Occlusion", graph);

	RGPassBuilder ssao = graph.AddPass("SSAO");
	ssao.Bind([=](CommandContext& renderContext, const RGPassResources& passResources)
		{
			renderContext.InsertResourceBarrier(pDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			renderContext.InsertResourceBarrier(pColor, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

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
			} shaderParameters{};

			shaderParameters.ProjectionInverse = camera.GetProjectionInverse();
			shaderParameters.ViewInverse = camera.GetViewInverse();
			shaderParameters.Projection = camera.GetProjection();
			shaderParameters.View = camera.GetView();
			shaderParameters.Dimensions.x = pColor->GetWidth();
			shaderParameters.Dimensions.y = pColor->GetHeight();
			shaderParameters.Near = camera.GetNear();
			shaderParameters.Far = camera.GetFar();
			shaderParameters.Power = g_AoPower;
			shaderParameters.Radius = g_AoRadius;
			shaderParameters.Threshold = g_AoThreshold;
			shaderParameters.Samples = g_AoSamples;

			renderContext.SetComputeDynamicConstantBufferView(0, &shaderParameters, sizeof(ShaderParameters));
			renderContext.BindResource(1, 0, pColor->GetUAV());
			renderContext.BindResource(2, 0, pDepth->GetSRV());

			int dispatchGroupsX = Math::DivideAndRoundUp(pColor->GetWidth(), 16);
			int dispatchGroupsY = Math::DivideAndRoundUp(pColor->GetHeight(), 16);
			renderContext.Dispatch(dispatchGroupsX, dispatchGroupsY);
		});

	RGPassBuilder blur = graph.AddPass("Blur SSAO");
	blur.Bind([=](CommandContext& renderContext, const RGPassResources& passResources)
		{
			renderContext.InsertResourceBarrier(m_pAmbientOcclusionIntermediate.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			renderContext.InsertResourceBarrier(pColor, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

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
			shaderParameters.DimensionsInv = Vector2(1.0f / pColor->GetWidth(), 1.0f / pColor->GetHeight());
			shaderParameters.Far = camera.GetFar();
			shaderParameters.Near = camera.GetNear();

			renderContext.SetComputeDynamicConstantBufferView(0, &shaderParameters, sizeof(ShaderParameters));
			renderContext.BindResource(1, 0, m_pAmbientOcclusionIntermediate->GetUAV());
			renderContext.BindResource(2, 0, pDepth->GetSRV());
			renderContext.BindResource(2, 1, pColor->GetSRV());

			renderContext.Dispatch(Math::DivideAndRoundUp(m_pAmbientOcclusionIntermediate->GetWidth(), 256), m_pAmbientOcclusionIntermediate->GetHeight());

			renderContext.InsertResourceBarrier(m_pAmbientOcclusionIntermediate.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			renderContext.InsertResourceBarrier(pColor, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			renderContext.BindResource(1, 0, pColor->GetUAV());
			renderContext.BindResource(2, 0, pDepth->GetSRV());
			renderContext.BindResource(2, 1, m_pAmbientOcclusionIntermediate->GetSRV());

			shaderParameters.Horizontal = 0;
			renderContext.SetComputeDynamicConstantBufferView(0, &shaderParameters, sizeof(ShaderParameters));
			renderContext.Dispatch(m_pAmbientOcclusionIntermediate->GetWidth(), Math::DivideAndRoundUp(m_pAmbientOcclusionIntermediate->GetHeight(), 256));
		});
}

void SSAO::SetupResources(Graphics* pGraphics)
{
	m_pAmbientOcclusionIntermediate = std::make_unique<Texture>(pGraphics, "SSAO Blurred");
}

void SSAO::SetupPipelines(Graphics* pGraphics)
{
	//SSAO
	{
		Shader* pComputeShader = pGraphics->GetShaderManager()->GetShader("SSAO.hlsl", ShaderType::Compute, "CSMain");

		m_pSSAORS = std::make_unique<RootSignature>(pGraphics);
		m_pSSAORS->FinalizeFromShader("SSAO", pComputeShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetRootSignature(m_pSSAORS->GetRootSignature());
		psoDesc.SetName("SSAO");
		m_pSSAOPSO = pGraphics->CreatePipeline(psoDesc);
	}

	//SSAO Blur
	{
		Shader* pComputeShader = pGraphics->GetShaderManager()->GetShader("SSAOBlur.hlsl", ShaderType::Compute, "CSMain");

		m_pSSAOBlurRS = std::make_unique<RootSignature>(pGraphics);
		m_pSSAOBlurRS->FinalizeFromShader("SSAO Blur", pComputeShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetRootSignature(m_pSSAOBlurRS->GetRootSignature());
		psoDesc.SetName("SSAO Blur");
		m_pSSAOBlurPSO = pGraphics->CreatePipeline(psoDesc);
	}
}
