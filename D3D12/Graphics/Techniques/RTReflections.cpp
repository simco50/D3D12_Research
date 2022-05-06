#include "stdafx.h"
#include "RTReflections.h"
#include "Graphics/RHI/RootSignature.h"
#include "Graphics/RHI/Graphics.h"
#include "Graphics/RHI/CommandContext.h"
#include "Graphics/RHI/Texture.h"
#include "Graphics/RHI/ShaderBindingTable.h"
#include "Graphics/RHI/ResourceViews.h"
#include "Graphics/RHI/StateObject.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/SceneView.h"

RTReflections::RTReflections(GraphicsDevice* pDevice)
{
	if (pDevice->GetCapabilities().SupportsRaytracing())
	{
		m_pGlobalRS = new RootSignature(pDevice);
		m_pGlobalRS->AddRootConstants(0, 1);
		m_pGlobalRS->AddConstantBufferView(100);
		m_pGlobalRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 4);
		m_pGlobalRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4);
		m_pGlobalRS->Finalize("Global");

		StateObjectInitializer stateDesc;
		stateDesc.Name = "RT Reflections";
		stateDesc.RayGenShader = "RayGen";
		stateDesc.AddLibrary("RayTracing/RTReflections.hlsl");
		stateDesc.AddLibrary("RayTracing/SharedRaytracingLib.hlsl", { "OcclusionMS", "MaterialCHS", "MaterialAHS", "MaterialMS" });
		stateDesc.AddHitGroup("ReflectionHitGroup", "MaterialCHS", "MaterialAHS");
		stateDesc.AddMissShader("MaterialMS");
		stateDesc.AddMissShader("OcclusionMiss");
		stateDesc.MaxPayloadSize = 6 * sizeof(float);
		stateDesc.MaxAttributeSize = 2 * sizeof(float);
		stateDesc.MaxRecursion = 2;
		stateDesc.pGlobalRootSignature = m_pGlobalRS;
		m_pRtSO = pDevice->CreateStateObject(stateDesc);
	}
}

void RTReflections::Execute(RGGraph& graph, const SceneView& view, SceneTextures& sceneTextures)
{
	RGHandle<Texture> reflectionsTarget = graph.CreateTexture("Reflections Target", graph.GetDesc(sceneTextures.ColorTarget));

	graph.AddCopyPass("Cache Scene Color", sceneTextures.ColorTarget, reflectionsTarget);

	graph.AddPass("RT Reflections", RGPassFlag::Compute)
		.Read({ sceneTextures.Normals, sceneTextures.Depth, sceneTextures.Roughness, reflectionsTarget })
		.Write(&sceneTextures.ColorTarget)
		.Bind([=](CommandContext& context, const RGPassResources& resources)
			{
				Texture* pTarget = resources.Get(sceneTextures.ColorTarget);

				context.SetComputeRootSignature(m_pGlobalRS);
				context.SetPipelineState(m_pRtSO);

				struct
				{
					float ViewPixelSpreadAngle;
				} parameters;

				parameters.ViewPixelSpreadAngle = atanf(2.0f * tanf(view.View.FoV / 2) / (float)pTarget->GetHeight());

				ShaderBindingTable bindingTable(m_pRtSO);
				bindingTable.BindRayGenShader("RayGen");
				bindingTable.BindMissShader("MaterialMS", 0);
				bindingTable.BindMissShader("OcclusionMS", 1);
				bindingTable.BindHitGroup("ReflectionHitGroup", 0);

				context.SetRootConstants(0, parameters);
				context.SetRootCBV(1, Renderer::GetViewUniforms(view, pTarget));
				context.BindResources(2, pTarget->GetUAV());
				context.BindResources(3, {
					resources.Get(sceneTextures.Depth)->GetSRV(),
					resources.Get(reflectionsTarget)->GetSRV(),
					resources.Get(sceneTextures.Normals)->GetSRV(),
					resources.Get(sceneTextures.Roughness)->GetSRV(),
					});

				context.DispatchRays(bindingTable, pTarget->GetWidth(), pTarget->GetHeight());
			});
}

