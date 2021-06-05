#include "stdafx.h"
#include "RTReflections.h"
#include "Graphics/Core/Shader.h"
#include "Graphics/Core/PipelineState.h"
#include "Graphics/Core/RootSignature.h"
#include "Graphics/Core/GraphicsBuffer.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/Core/CommandContext.h"
#include "Graphics/Core/Texture.h"
#include "Graphics/Core/ShaderBindingTable.h"
#include "Graphics/Core/ResourceViews.h"
#include "Graphics/Core/StateObject.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/Mesh.h"
#include "Scene/Camera.h"
#include "DemoApp.h"

RTReflections::RTReflections(GraphicsDevice* pDevice)
	: m_pDevice(pDevice)
{
	if (pDevice->GetCapabilities().SupportsRaytracing())
	{
		SetupPipelines(pDevice);
	}
}

void RTReflections::Execute(RGGraph& graph, const SceneData& sceneData)
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
				Matrix View;
				Matrix ViewInverse;
				Matrix ProjectionInverse;
				uint32 NumLights;
				float ViewPixelSpreadAngle;
				uint32 TLASIndex;
			} parameters{};

			parameters.View = sceneData.pCamera->GetView();
			parameters.ViewInverse = sceneData.pCamera->GetViewInverse();
			parameters.ProjectionInverse = sceneData.pCamera->GetProjectionInverse();
			parameters.NumLights = sceneData.pLightBuffer->GetNumElements();
			parameters.ViewPixelSpreadAngle = atanf(2.0f * tanf(sceneData.pCamera->GetFoV() / 2) / (float)m_pSceneColor->GetHeight());
			parameters.TLASIndex = sceneData.SceneTLAS;

			ShaderBindingTable bindingTable(m_pRtSO);
			bindingTable.BindRayGenShader("RayGen");
			bindingTable.BindMissShader("ReflectionMiss", 0);
			bindingTable.BindMissShader("ShadowMiss", 1);
			bindingTable.BindHitGroup("ReflectionHitGroup", 0);

			struct HitData
			{
				MaterialData Material;
				uint32 VertexBuffer;
				uint32 IndexBuffer;
			};

			DynamicAllocation allocation = context.AllocateTransientMemory(sizeof(HitData) * sceneData.Batches.size());
			HitData* pCurrent = (HitData*)allocation.pMappedMemory;

			for (const Batch& b : sceneData.Batches)
			{
				HitData& hitData = *pCurrent;
				hitData.Material = b.Material;
				hitData.VertexBuffer = b.VertexBufferDescriptor;
				hitData.IndexBuffer = b.IndexBufferDescriptor;
				++pCurrent;
			}

			const D3D12_CPU_DESCRIPTOR_HANDLE srvs[] = {
				sceneData.pLightBuffer->GetSRV()->GetDescriptor(),
				sceneData.pLightBuffer->GetSRV()->GetDescriptor() /*dummy*/,
				sceneData.pResolvedDepth->GetSRV()->GetDescriptor(),
				m_pSceneColor->GetSRV()->GetDescriptor(),
				sceneData.pResolvedNormals->GetSRV()->GetDescriptor(),
			};

			context.SetComputeDynamicConstantBufferView(0, parameters);
			context.SetComputeDynamicConstantBufferView(1, *sceneData.pShadowData);
			context.BindResource(2, 0, sceneData.pResolvedTarget->GetUAV());
			context.BindResources(3, 0, srvs, ARRAYSIZE(srvs));
			context.SetComputeRootSRV(4, allocation.GpuHandle);
			context.BindResourceTable(5, sceneData.GlobalSRVHeapHandle.GpuHandle, CommandListContext::Compute);

			context.DispatchRays(bindingTable, sceneData.pResolvedTarget->GetWidth(), sceneData.pResolvedTarget->GetHeight());
		});
}

void RTReflections::OnResize(uint32 width, uint32 height)
{
	m_pSceneColor = m_pDevice->CreateTexture(TextureDesc::Create2D(width, height, GraphicsDevice::RENDER_TARGET_FORMAT, TextureFlag::ShaderResource, 1, 1), "SceneColor Copy");
}

void RTReflections::SetupPipelines(GraphicsDevice* pDevice)
{
	ShaderLibrary* pShaderLibrary = pDevice->GetLibrary("RTReflections.hlsl");

	m_pHitSignature = std::make_unique<RootSignature>(pDevice);
	m_pHitSignature->Finalize("Hit", D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);

	m_pGlobalRS = std::make_unique<RootSignature>(pDevice);
	m_pGlobalRS->FinalizeFromShader("Global", pShaderLibrary);

	StateObjectInitializer stateDesc;
	stateDesc.Name = "RT Reflections";
	stateDesc.RayGenShader = "RayGen";
	stateDesc.AddLibrary(pShaderLibrary, { "RayGen", "ReflectionClosestHit", "ReflectionMiss", "ShadowMiss", "ReflectionAnyHit" });
	stateDesc.AddHitGroup("ReflectionHitGroup", "ReflectionClosestHit", "ReflectionAnyHit", "", m_pHitSignature.get());
	stateDesc.AddMissShader("ReflectionMiss");
	stateDesc.AddMissShader("ShadowMiss");
	stateDesc.MaxPayloadSize = 5 * sizeof(float);
	stateDesc.MaxAttributeSize = 2 * sizeof(float);
	stateDesc.MaxRecursion = 2;
	stateDesc.pGlobalRootSignature = m_pGlobalRS.get();
	m_pRtSO = pDevice->CreateStateObject(stateDesc);
}
