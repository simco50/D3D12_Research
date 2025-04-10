#include "stdafx.h"
#include "PathTracing.h"
#include "RHI/Device.h"
#include "RHI/Texture.h"
#include "RHI/StateObject.h"
#include "RHI/PipelineState.h"
#include "RHI/RootSignature.h"
#include "RHI/CommandContext.h"
#include "RHI/ShaderBindingTable.h"
#include "Renderer/Renderer.h"
#include "RenderGraph/RenderGraph.h"

PathTracing::PathTracing(GraphicsDevice* pDevice)
{
	if (!pDevice->GetCapabilities().SupportsRaytracing())
	{
		return;
	}

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
	desc.pGlobalRootSignature = GraphicsCommon::pCommonRS;
	m_pSO = pDevice->CreateStateObject(desc);

	m_pBlitPSO = pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "RayTracing/PathTracing.hlsl", "BlitAccumulationCS", { "BLIT_SHADER"});

	m_OnShaderCompiledHandle = pDevice->GetShaderManager()->OnShaderEditedEvent().AddLambda([this](Shader*) { Reset(); });
}

void PathTracing::Render(RGGraph& graph, const RenderView* pView, RGTexture* pTarget)
{
	if (!IsSupported())
	{
		return;
	}

	TextureDesc desc = pTarget->GetDesc();
	desc.Flags |= TextureFlag::ShaderResource;
	RGTexture* pAccumulationTexture = RGUtils::CreatePersistent(graph, "Accumulation Target", desc, &m_pAccumulationTexture);

	static int32 numBounces = 3;
	static int32 numSamples = 200;

	bool doReset = false;
	if (ImGui::Begin("Settings"))
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

	if (pView->WorldToClipUnjittered != m_LastViewProjection)
		doReset = true;

	if (doReset)
		Reset();

	struct PassParams
	{
		uint32 NumBounces;
		uint32 AccumulatedFrames;
		RWTextureView Output;
		RWTextureView AccumulationTarget;
		TextureView Accumulation;
	};

	if (m_NumAccumulatedFrames >= numSamples)
	{
		graph.AddPass("Blit", RGPassFlag::Compute)
			.Read(pAccumulationTexture)
			.Write(pTarget)
			.Bind([=](CommandContext& context, const RGResources& resources)
				{
					context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
					context.SetPipelineState(m_pBlitPSO);

					PassParams parameters{};
					parameters.NumBounces		 = numBounces;
					parameters.AccumulatedFrames = m_NumAccumulatedFrames;
					parameters.Output			 = resources.GetUAV(pTarget);
					parameters.Accumulation		 = resources.GetSRV(pAccumulationTexture);
					context.BindRootSRV(BindingSlot::PerInstance, parameters);

					context.Dispatch(ComputeUtils::GetNumThreadGroups(pTarget->GetDesc().Width, 8, pTarget->GetDesc().Height, 8));
				});
	}
	else
	{
		m_LastViewProjection = pView->WorldToClipUnjittered;
		m_NumAccumulatedFrames++;

		graph.AddPass("Path Tracing", RGPassFlag::Compute)
			.Write({ pTarget, pAccumulationTexture })
			.Bind([=](CommandContext& context, const RGResources& resources)
				{
					Texture* pRTTarget = resources.Get(pTarget);

					context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
					context.SetPipelineState(m_pSO);

					PassParams parameters{};
					parameters.NumBounces		  = numBounces;
					parameters.AccumulatedFrames  = m_NumAccumulatedFrames;
					parameters.Output			  = resources.GetUAV(pTarget);
					parameters.AccumulationTarget = resources.GetUAV(pAccumulationTexture);
					context.BindRootSRV(BindingSlot::PerInstance, parameters);

					Renderer::BindViewUniforms(context, *pView);

					ShaderBindingTable bindingTable(m_pSO);
					bindingTable.BindRayGenShader("RayGen");
					bindingTable.BindMissShader("MaterialMS", 0);
					bindingTable.BindMissShader("OcclusionMS", 1);
					bindingTable.BindHitGroup("MaterialHG", 0);


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
