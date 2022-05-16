#include "stdafx.h"
#include "PathTracing.h"
#include "Graphics/RHI/Graphics.h"
#include "Graphics/RHI/Texture.h"
#include "Graphics/RHI/StateObject.h"
#include "Graphics/RHI/RootSignature.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RHI/CommandContext.h"
#include "Graphics/RHI/ShaderBindingTable.h"
#include "Graphics/SceneView.h"

PathTracing::PathTracing(GraphicsDevice* pDevice)
	: m_pDevice(pDevice)
{
	if (!IsSupported())
	{
		return;
	}

	m_pRS = new RootSignature(pDevice);
	m_pRS->AddConstantBufferView(0);
	m_pRS->AddConstantBufferView(100);
	m_pRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2);
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

	m_OnShaderCompiledHandle = pDevice->GetShaderManager()->OnLibraryRecompiledEvent().AddLambda([this](ShaderLibrary*, ShaderLibrary*)
		{
			Reset();
		});
}

PathTracing::~PathTracing()
{
	if (m_OnShaderCompiledHandle.IsValid())
	{
		m_pDevice->GetShaderManager()->OnLibraryRecompiledEvent().Remove(m_OnShaderCompiledHandle);
	}
}

void PathTracing::Render(RGGraph& graph, const SceneView* pView, RGTexture* pTarget)
{
	if (!IsSupported())
	{
		return;
	}

	RGTexture* pAccumulationTexture = RGUtils::CreatePersistentTexture(graph, "Accumulation Target", pTarget->GetDesc(), &m_pAccumulationTexture, true);

	static int32 numBounces = 3;

	if (ImGui::Begin("Parameters"))
	{
		if (ImGui::CollapsingHeader("Path Tracing"))
		{
			if (ImGui::SliderInt("Bounces", &numBounces, 1, 15))
			{
				Reset();
			}
			if (ImGui::Button("Reset"))
			{
				Reset();
			}
		}
	}
	ImGui::End();

	if (pView->View.PreviousViewProjection != pView->View.ViewProjection)
	{
		Reset();
	}

	m_NumAccumulatedFrames++;

	graph.AddPass("Path Tracing", RGPassFlag::Compute)
		.Write({ pTarget, pAccumulationTexture })
		.Bind([=](CommandContext& context, const RGPassResources& resources)
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

				context.SetRootCBV(0, parameters);
				context.SetRootCBV(1, Renderer::GetViewUniforms(pView, pRTTarget));
				context.BindResources(2, {
					pRTTarget->GetUAV(),
					pAccumulationTexture->Get()->GetUAV(),
					});

				context.DispatchRays(bindingTable, pRTTarget->GetWidth(), pRTTarget->GetHeight());
			});
}

void PathTracing::Reset()
{
	m_NumAccumulatedFrames = 0;
}

bool PathTracing::IsSupported()
{
	return m_pDevice->GetCapabilities().SupportsRaytracing();
}
