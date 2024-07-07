#include "stdafx.h"
#include "RTReflections.h"
#include "RHI/RootSignature.h"
#include "RHI/Device.h"
#include "RHI/CommandContext.h"
#include "RHI/Texture.h"
#include "RHI/ShaderBindingTable.h"
#include "RHI/ResourceViews.h"
#include "RHI/StateObject.h"
#include "Renderer/RenderGraph/RenderGraph.h"
#include "Renderer/SceneView.h"

RTReflections::RTReflections(GraphicsDevice* pDevice)
{
	if (pDevice->GetCapabilities().SupportsRaytracing())
	{
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
		stateDesc.pGlobalRootSignature = GraphicsCommon::pCommonRS;
		m_pRtSO = pDevice->CreateStateObject(stateDesc);
	}
}

void RTReflections::Execute(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures)
{
	RGTexture* pReflectionsTarget = graph.Create("Scene Color", sceneTextures.pColorTarget->GetDesc());

	graph.AddPass("RT Reflections", RGPassFlag::Compute)
		.Read({ sceneTextures.pNormals, sceneTextures.pDepth, sceneTextures.pRoughness, sceneTextures.pColorTarget })
		.Write(pReflectionsTarget)
		.Bind([=](CommandContext& context, const RGResources& resources)
			{
				Texture* pTarget = resources.Get(pReflectionsTarget);

				context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
				context.SetPipelineState(m_pRtSO);

				struct
				{
					float ViewPixelSpreadAngle;
				} parameters;

				parameters.ViewPixelSpreadAngle = atanf(2.0f * tanf(pView->MainView.FoV / 2) / (float)pTarget->GetHeight());

				ShaderBindingTable bindingTable(m_pRtSO);
				bindingTable.BindRayGenShader("RayGen");
				bindingTable.BindMissShader("MaterialMS", 0);
				bindingTable.BindMissShader("OcclusionMS", 1);
				bindingTable.BindHitGroup("ReflectionHitGroup", 0);

				context.BindRootCBV(0, parameters);
				context.BindRootCBV(1, Renderer::GetViewUniforms(pView, pTarget));
				context.BindResources(2, pTarget->GetUAV());
				context.BindResources(3, {
					resources.GetSRV(sceneTextures.pDepth),
					resources.GetSRV(sceneTextures.pColorTarget),
					resources.GetSRV(sceneTextures.pNormals),
					resources.GetSRV(sceneTextures.pRoughness),
					});

				context.DispatchRays(bindingTable, pTarget->GetWidth(), pTarget->GetHeight());
			});

	sceneTextures.pColorTarget = pReflectionsTarget;
}

