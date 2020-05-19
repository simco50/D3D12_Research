#include "stdafx.h"
#include "SSAO.h"
#include "Graphics/Core/Shader.h"
#include "Graphics/Core/PipelineState.h"
#include "Graphics/Core/RootSignature.h"
#include "Graphics/Core/GraphicsBuffer.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/Core/CommandContext.h"
#include "Graphics/Core/CommandQueue.h"
#include "Graphics/Core/Texture.h"
#include "Graphics/Core/ResourceViews.h"
#include "Graphics/Mesh.h"
#include "Graphics/Light.h"
#include "Graphics/Profiler.h"
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

void SSAO::Execute(RGGraph& graph, const SsaoInputResources& resources)
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

	graph.AddPass("SSAO", [&](RGPassBuilder& builder)
		{
			return [=](CommandContext& renderContext, const RGPassResources& passResources)
			{
				renderContext.InsertResourceBarrier(resources.pDepthTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				renderContext.InsertResourceBarrier(resources.pNormalsTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				renderContext.InsertResourceBarrier(resources.pRenderTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

				renderContext.SetComputeRootSignature(m_pSSAORS.get());
				renderContext.SetPipelineState(m_pSSAOPSO.get());

				struct ShaderParameters
				{
					Matrix ProjectionInverse;
					Matrix Projection;
					Matrix View;
					uint32 Dimensions[2];
					float Near;
					float Far;
					float Power;
					float Radius;
					float Threshold;
					int Samples;
				} shaderParameters{};

				shaderParameters.ProjectionInverse = resources.pCamera->GetProjectionInverse();
				shaderParameters.Projection = resources.pCamera->GetProjection();
				shaderParameters.View = resources.pCamera->GetView();
				shaderParameters.Dimensions[0] = resources.pRenderTarget->GetWidth();
				shaderParameters.Dimensions[1] = resources.pRenderTarget->GetHeight();
				shaderParameters.Near = resources.pCamera->GetNear();
				shaderParameters.Far = resources.pCamera->GetFar();
				shaderParameters.Power = g_AoPower;
				shaderParameters.Radius = g_AoRadius;
				shaderParameters.Threshold = g_AoThreshold;
				shaderParameters.Samples = g_AoSamples;

				renderContext.SetComputeDynamicConstantBufferView(0, &shaderParameters, sizeof(ShaderParameters));
				renderContext.SetDynamicDescriptor(1, 0, resources.pRenderTarget->GetUAV());
				renderContext.SetDynamicDescriptor(2, 0, resources.pDepthTexture->GetSRV());
				renderContext.SetDynamicDescriptor(2, 1, resources.pNormalsTexture->GetSRV());

				int dispatchGroupsX = Math::DivideAndRoundUp(resources.pRenderTarget->GetWidth(), 16);
				int dispatchGroupsY = Math::DivideAndRoundUp(resources.pRenderTarget->GetHeight(), 16);
				renderContext.Dispatch(dispatchGroupsX, dispatchGroupsY);
			};
		});

	graph.AddPass("Blur SSAO", [&](RGPassBuilder& builder)
		{
			return [=](CommandContext& renderContext, const RGPassResources& passResources)
			{
				renderContext.InsertResourceBarrier(m_pAmbientOcclusionIntermediate.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				renderContext.InsertResourceBarrier(resources.pRenderTarget, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

				renderContext.SetComputeRootSignature(m_pSSAOBlurRS.get());
				renderContext.SetPipelineState(m_pSSAOBlurPSO.get());

				struct ShaderParameters
				{
					float Dimensions[2];
					uint32 Horizontal;
					float Far;
					float Near;
				} shaderParameters;

				shaderParameters.Horizontal = 1;
				shaderParameters.Dimensions[0] = 1.0f / resources.pRenderTarget->GetWidth();
				shaderParameters.Dimensions[1] = 1.0f / resources.pRenderTarget->GetHeight();
				shaderParameters.Far = resources.pCamera->GetFar();
				shaderParameters.Near = resources.pCamera->GetNear();

				renderContext.SetComputeDynamicConstantBufferView(0, &shaderParameters, sizeof(ShaderParameters));
				renderContext.SetDynamicDescriptor(1, 0, m_pAmbientOcclusionIntermediate->GetUAV());
				renderContext.SetDynamicDescriptor(2, 0, resources.pDepthTexture->GetSRV());
				renderContext.SetDynamicDescriptor(2, 1, resources.pRenderTarget->GetSRV());

				renderContext.Dispatch(Math::DivideAndRoundUp(m_pAmbientOcclusionIntermediate->GetWidth(), 256), m_pAmbientOcclusionIntermediate->GetHeight());

				renderContext.InsertResourceBarrier(m_pAmbientOcclusionIntermediate.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				renderContext.InsertResourceBarrier(resources.pRenderTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

				renderContext.SetDynamicDescriptor(1, 0, resources.pRenderTarget->GetUAV());
				renderContext.SetDynamicDescriptor(2, 0, resources.pDepthTexture->GetSRV());
				renderContext.SetDynamicDescriptor(2, 1, m_pAmbientOcclusionIntermediate->GetSRV());

				shaderParameters.Horizontal = 0;
				renderContext.SetComputeDynamicConstantBufferView(0, &shaderParameters, sizeof(ShaderParameters));
				renderContext.Dispatch(m_pAmbientOcclusionIntermediate->GetWidth(), Math::DivideAndRoundUp(m_pAmbientOcclusionIntermediate->GetHeight(), 256));
			};
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
		Shader computeShader("Resources/Shaders/SSAO.hlsl", Shader::Type::Compute, "CSMain");

		m_pSSAORS = std::make_unique<RootSignature>();
		m_pSSAORS->FinalizeFromShader("SSAO", computeShader, pGraphics->GetDevice());

		m_pSSAOPSO = std::make_unique<PipelineState>();
		m_pSSAOPSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pSSAOPSO->SetRootSignature(m_pSSAORS->GetRootSignature());
		m_pSSAOPSO->Finalize("SSAO PSO", pGraphics->GetDevice());
	}

	//SSAO Blur
	{
		Shader computeShader("Resources/Shaders/SSAOBlur.hlsl", Shader::Type::Compute, "CSMain");

		m_pSSAOBlurRS = std::make_unique<RootSignature>();
		m_pSSAOBlurRS->FinalizeFromShader("SSAO Blur", computeShader, pGraphics->GetDevice());

		m_pSSAOBlurPSO = std::make_unique<PipelineState>();
		m_pSSAOBlurPSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pSSAOBlurPSO->SetRootSignature(m_pSSAOBlurRS->GetRootSignature());
		m_pSSAOBlurPSO->Finalize("SSAO Blur PSO", pGraphics->GetDevice());
	}
}