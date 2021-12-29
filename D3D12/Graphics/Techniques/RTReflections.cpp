#include "stdafx.h"
#include "RTReflections.h"
#include "Graphics/Core/Shader.h"
#include "Graphics/Core/PipelineState.h"
#include "Graphics/Core/RootSignature.h"
#include "Graphics/Core/Buffer.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/Core/CommandContext.h"
#include "Graphics/Core/Texture.h"
#include "Graphics/Core/ShaderBindingTable.h"
#include "Graphics/Core/ResourceViews.h"
#include "Graphics/Core/StateObject.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/Mesh.h"
#include "Scene/Camera.h"
#include "Graphics/SceneView.h"

RTReflections::RTReflections(GraphicsDevice* pDevice)
	: m_pDevice(pDevice)
{
	if (pDevice->GetCapabilities().SupportsRaytracing())
	{
		SetupPipelines(pDevice);
	}
}

void RTReflections::Execute(RGGraph& graph, const SceneView& sceneData)
{
	RGPassBuilder rt = graph.AddPass("RT Reflections");
	rt.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
		{
			context.CopyTexture(sceneData.pResolvedTarget, m_pSceneColor.get());

			context.InsertResourceBarrier(sceneData.pResolvedDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(sceneData.pResolvedNormals, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pSceneColor.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(sceneData.pResolvedTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			context.SetComputeRootSignature(m_pGlobalRS.get());
			context.SetPipelineState(m_pRtSO);

			struct Parameters
			{
				float ViewPixelSpreadAngle;
			} parameters{};

			parameters.ViewPixelSpreadAngle = atanf(2.0f * tanf(sceneData.pCamera->GetFoV() / 2) / (float)m_pSceneColor->GetHeight());

			ShaderBindingTable bindingTable(m_pRtSO);
			bindingTable.BindRayGenShader("RayGen");
			bindingTable.BindMissShader("ReflectionMiss", 0);
			bindingTable.BindMissShader("ShadowMiss", 1);
			bindingTable.BindHitGroup("ReflectionHitGroup", 0);

			const D3D12_CPU_DESCRIPTOR_HANDLE srvs[] = {
				sceneData.pResolvedDepth->GetSRV()->GetDescriptor(),
				m_pSceneColor->GetSRV()->GetDescriptor(),
				sceneData.pResolvedNormals->GetSRV()->GetDescriptor(),
			};

			context.SetRootCBV(0, parameters);
			context.SetRootCBV(1, GetViewUniforms(sceneData));
			context.BindResource(2, 0, sceneData.pResolvedTarget->GetUAV());
			context.BindResources(3, 0, srvs, ARRAYSIZE(srvs));

			context.DispatchRays(bindingTable, sceneData.pResolvedTarget->GetWidth(), sceneData.pResolvedTarget->GetHeight());
		});
}

void RTReflections::OnResize(uint32 width, uint32 height)
{
	m_pSceneColor = m_pDevice->CreateTexture(TextureDesc::Create2D(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, TextureFlag::ShaderResource, 1, 1), "SceneColor Copy");
}

void RTReflections::SetupPipelines(GraphicsDevice* pDevice)
{
	ShaderLibrary* pShaderLibrary = pDevice->GetLibrary("RTReflections.hlsl");

	m_pGlobalRS = std::make_unique<RootSignature>(pDevice);
	m_pGlobalRS->FinalizeFromShader("Global", pShaderLibrary);

	StateObjectInitializer stateDesc;
	stateDesc.Name = "RT Reflections";
	stateDesc.RayGenShader = "RayGen";
	stateDesc.AddLibrary(pShaderLibrary, { "RayGen", "ReflectionClosestHit", "ReflectionMiss", "ShadowMiss", "ReflectionAnyHit" });
	stateDesc.AddHitGroup("ReflectionHitGroup", "ReflectionClosestHit", "ReflectionAnyHit");
	stateDesc.AddMissShader("ReflectionMiss");
	stateDesc.AddMissShader("ShadowMiss");
	stateDesc.MaxPayloadSize = 5 * sizeof(float);
	stateDesc.MaxAttributeSize = 2 * sizeof(float);
	stateDesc.MaxRecursion = 2;
	stateDesc.pGlobalRootSignature = m_pGlobalRS.get();
	m_pRtSO = pDevice->CreateStateObject(stateDesc);
}
