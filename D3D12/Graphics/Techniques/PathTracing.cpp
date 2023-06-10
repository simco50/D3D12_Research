#include "stdafx.h"
#include "PathTracing.h"
#include "Graphics/RHI/Graphics.h"
#include "Graphics/RHI/Texture.h"
#include "Graphics/RHI/StateObject.h"
#include "Graphics/RHI/PipelineState.h"
#include "Graphics/RHI/RootSignature.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RHI/CommandContext.h"
#include "Graphics/RHI/ShaderBindingTable.h"
#include "Graphics/SceneView.h"

PathTracing::PathTracing(GraphicsDevice* pDevice)
{
	if (!pDevice->GetCapabilities().SupportsRaytracing())
	{
		return;
	}

	m_pRS = new RootSignature(pDevice);
	m_pRS->AddRootCBV(0);
	m_pRS->AddRootCBV(100);
	m_pRS->AddDescriptorTable(0, 2, D3D12_DESCRIPTOR_RANGE_TYPE_UAV);
	m_pRS->AddDescriptorTable(0, 2, D3D12_DESCRIPTOR_RANGE_TYPE_SRV);
	m_pRS->Finalize("Global");

	StateObjectInitializer desc{};
	desc.Name = "Path Tracing";
	desc.MaxRecursion = 1;
	desc.MaxPayloadSize = 6 * sizeof(float);
	desc.MaxAttributeSize = 2 * sizeof(float);
	desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
	desc.AddLibrary("RayTracing/PathTracing.hlsl");
	desc.AddLibrary("RayTracing/SharedRaytracingLib.hlsl", {"OcclusionMS", "MaterialCHS", "MaterialAHS", "MaterialMS"});
	desc.AddHitGroup("MaterialHG", "MaterialCHS", "MaterialAHS");
	desc.AddMissShader("MaterialMS");
	desc.AddMissShader("OcclusionMiss");
	desc.pGlobalRootSignature = m_pRS;
	m_pSO = pDevice->CreateStateObject(desc);

	m_pBlitPSO = pDevice->CreateComputePipeline(m_pRS, "RayTracing/PathTracing.hlsl", "BlitAccumulationCS", { "BLIT_SHADER"});

	m_OnShaderCompiledHandle = pDevice->GetShaderManager()->OnShaderEditedEvent().AddLambda([this](Shader*) { Reset(); });
}

void PathTracing::Render(RGGraph& graph, const SceneView* pView, RGTexture* pTarget)
{
	if (!IsSupported())
	{
		return;
	}

	RGTexture* pAccumulationTexture = RGUtils::CreatePersistent(graph, "Accumulation Target", pTarget->GetDesc(), &m_pAccumulationTexture, true);

	static int32 numBounces = 3;
	static int32 numSamples = 200;

	bool doReset = false;
	if (ImGui::Begin("Parameters"))
	{
		if (ImGui::CollapsingHeader("Path Tracing"))
		{
			if (ImGui::SliderInt("Bounces", &numBounces, 1, 15))
			{
				doReset = true;
			}
			if (ImGui::SliderInt("Samples", &numSamples, 1, 1500, "%d", ImGuiSliderFlags_Logarithmic))
			{
				if (numSamples < m_NumAccumulatedFrames)
					doReset = true;
			}
			if (ImGui::Button("Reset"))
			{
				doReset = true;
			}
		}
	}
	ImGui::End();

	if (pView->MainView.UnjtteredViewProjection != m_LastViewProjection)
		doReset = true;

	if (doReset)
		Reset();

	if (m_NumAccumulatedFrames >= numSamples)
	{
		graph.AddPass("Blit", RGPassFlag::Compute)
			.Read(pAccumulationTexture)
			.Write(pTarget)
			.Bind([=](CommandContext& context)
				{
					context.SetComputeRootSignature(m_pRS);
					context.SetPipelineState(m_pBlitPSO);

					struct
					{
						uint32 NumBounces;
						uint32 AccumulatedFrames;
					} parameters;

					parameters.NumBounces = numBounces;
					parameters.AccumulatedFrames = m_NumAccumulatedFrames;

					context.BindRootCBV(0, parameters);
					context.BindResources(2, pTarget->Get()->GetUAV());
					context.BindResources(3, pAccumulationTexture->Get()->GetSRV());
					context.Dispatch(ComputeUtils::GetNumThreadGroups(pTarget->GetDesc().Width, 8, pTarget->GetDesc().Height, 8));
				});
	}
	else
	{
		m_LastViewProjection = pView->MainView.UnjtteredViewProjection;
		m_NumAccumulatedFrames++;

		graph.AddPass("Path Tracing", RGPassFlag::Compute)
			.Write({ pTarget, pAccumulationTexture })
			.Bind([=](CommandContext& context)
				{
					Texture* pRTTarget = pTarget->Get();

					context.SetComputeRootSignature(m_pRS);
					context.SetPipelineState(m_pSO);

					struct
					{
						uint32 NumBounces;
						uint32 AccumulatedFrames;
					} parameters;

					parameters.NumBounces = numBounces;
					parameters.AccumulatedFrames = m_NumAccumulatedFrames;

					ShaderBindingTable bindingTable(m_pSO);
					bindingTable.BindRayGenShader("RayGen");
					bindingTable.BindMissShader("MaterialMS", 0);
					bindingTable.BindMissShader("OcclusionMS", 1);
					bindingTable.BindHitGroup("MaterialHG", 0);

					context.BindRootCBV(0, parameters);
					context.BindRootCBV(1, Renderer::GetViewUniforms(pView, pRTTarget));
					context.BindResources(2, {
						pRTTarget->GetUAV(),
						pAccumulationTexture->Get()->GetUAV(),
						});

					context.DispatchRays(bindingTable, pRTTarget->GetWidth(), pRTTarget->GetHeight());
				});
	}
}

void PathTracing::Reset()
{
	m_NumAccumulatedFrames = 0;
}

bool PathTracing::IsSupported()
{
	return m_pSO != nullptr;
}
