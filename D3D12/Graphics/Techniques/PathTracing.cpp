#include "stdafx.h"
#include "PathTracing.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/Core/Texture.h"
#include "Graphics/Core/StateObject.h"
#include "Graphics/Core/RootSignature.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/Core/CommandContext.h"
#include "Graphics/Core/ShaderBindingTable.h"
#include "Scene/Camera.h"
#include "DemoApp.h"

PathTracing::PathTracing(GraphicsDevice* pDevice)
	: m_pDevice(pDevice)
{
	ShaderLibrary* pLibrary = pDevice->GetLibrary("PathTracing.hlsl");

	m_pRS = std::make_unique<RootSignature>(pDevice);
	m_pRS->FinalizeFromShader("Global", pLibrary);

	StateObjectInitializer desc{};
	desc.Name = "Path Tracing";
	desc.MaxRecursion = 2;
	desc.MaxPayloadSize = sizeof(Vector4);
	desc.MaxAttributeSize = 2 * sizeof(float);
	desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
	desc.AddLibrary(pLibrary);
	desc.AddHitGroup("PrimaryHG", "PrimaryCHS", "PrimaryAHS");
	desc.AddMissShader("PrimaryMS");
	desc.AddMissShader("ShadowMS");
	desc.pGlobalRootSignature = m_pRS.get();
	m_pSO = pDevice->CreateStateObject(desc);
}

PathTracing::~PathTracing()
{

}

void PathTracing::Render(RGGraph& graph, const SceneData& sceneData)
{
	RGPassBuilder rt = graph.AddPass("Path Tracing");
	rt.Bind([=](CommandContext& context, const RGPassResources& /* passResources */)
		{
			context.InsertResourceBarrier(sceneData.pRenderTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			context.SetComputeRootSignature(m_pRS.get());
			context.SetPipelineState(m_pSO);

			struct Parameters
			{
				Matrix View;
				Matrix ViewInverse;
				Matrix ProjectionInverse;
				Matrix Projection;
				uint32 NumLights;
				float ViewPixelSpreadAngle;
				uint32 TLASIndex;
			} parameters{};

			parameters.View = sceneData.pCamera->GetView();
			parameters.ViewInverse = sceneData.pCamera->GetViewInverse();
			parameters.ProjectionInverse = sceneData.pCamera->GetProjectionInverse();
			parameters.Projection = sceneData.pCamera->GetProjection();
			parameters.NumLights = sceneData.pLightBuffer->GetNumElements();
			parameters.ViewPixelSpreadAngle = atanf(2.0f * tanf(sceneData.pCamera->GetFoV() / 2) / (float)sceneData.pRenderTarget->GetHeight());
			parameters.TLASIndex = sceneData.SceneTLAS;

			ShaderBindingTable bindingTable(m_pSO);
			bindingTable.BindRayGenShader("RayGen");
			bindingTable.BindMissShader("PrimaryMS", 0);
			bindingTable.BindMissShader("ShadowMS", 1);
			bindingTable.BindHitGroup("PrimaryHG", 0);

			const D3D12_CPU_DESCRIPTOR_HANDLE srvs[] = {
				sceneData.pLightBuffer->GetSRV()->GetDescriptor(),
				sceneData.pLightBuffer->GetSRV()->GetDescriptor() /*dummy*/,
				sceneData.pResolvedDepth->GetSRV()->GetDescriptor(),
				sceneData.pPreviousColor->GetSRV()->GetDescriptor(),
				sceneData.pResolvedNormals->GetSRV()->GetDescriptor(),
				sceneData.pMaterialBuffer->GetSRV()->GetDescriptor(),
				sceneData.pMeshBuffer->GetSRV()->GetDescriptor(),
			};

			context.SetComputeDynamicConstantBufferView(0, parameters);
			context.SetComputeDynamicConstantBufferView(1, *sceneData.pShadowData);
			context.BindResource(2, 0, sceneData.pRenderTarget->GetUAV());
			context.BindResources(3, 0, srvs, ARRAYSIZE(srvs));
			context.BindResourceTable(4, sceneData.GlobalSRVHeapHandle.GpuHandle, CommandListContext::Compute);

			context.DispatchRays(bindingTable, sceneData.pRenderTarget->GetWidth(), sceneData.pRenderTarget->GetHeight());
		});
}

void PathTracing::OnResize(uint32 width, uint32 height)
{
}
