#include "stdafx.h"
#include "PathTracing.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/Core/Texture.h"
#include "Graphics/Core/StateObject.h"
#include "Graphics/Core/RootSignature.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/Core/CommandContext.h"
#include "Graphics/Core/ShaderBindingTable.h"
#include "Graphics/SceneView.h"
#include "Scene/Camera.h"

PathTracing::PathTracing(GraphicsDevice* pDevice)
	: m_pDevice(pDevice)
{
	if (!IsSupported())
	{
		return;
	}

	ShaderLibrary* pLibrary = pDevice->GetLibrary("PathTracing.hlsl");

	m_pRS = std::make_unique<RootSignature>(pDevice);
	m_pRS->FinalizeFromShader("Global", pLibrary);

	StateObjectInitializer desc{};
	desc.Name = "Path Tracing";
	desc.MaxRecursion = 1;
	desc.MaxPayloadSize = 13 * sizeof(float);
	desc.MaxAttributeSize = 2 * sizeof(float);
	desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
	desc.AddLibrary(pLibrary);
	desc.AddHitGroup("PrimaryHG", "PrimaryCHS", "PrimaryAHS");
	desc.AddMissShader("PrimaryMS");
	desc.AddMissShader("ShadowMS");
	desc.pGlobalRootSignature = m_pRS.get();
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

void PathTracing::Render(RGGraph& graph, const SceneView& sceneData)
{
	if (!IsSupported())
	{
		return;
	}

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
		ImGui::End();
	}

	if (sceneData.pCamera->GetPreviousViewProjection() != sceneData.pCamera->GetViewProjection())
	{
		Reset();
	}

	m_NumAccumulatedFrames++;

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
				uint32 TLASIndex;
				uint32 FrameIndex;
				uint32 NumBounces;
				uint32 AccumulatedFrames;
			} parameters{};

			parameters.View = sceneData.pCamera->GetView();
			parameters.ViewInverse = sceneData.pCamera->GetViewInverse();
			parameters.ProjectionInverse = sceneData.pCamera->GetProjectionInverse();
			parameters.Projection = sceneData.pCamera->GetProjection();
			parameters.NumLights = sceneData.pLightBuffer->GetNumElements();
			parameters.TLASIndex = sceneData.SceneTLAS;
			parameters.FrameIndex = sceneData.FrameIndex;
			parameters.NumBounces = numBounces;
			parameters.AccumulatedFrames = m_NumAccumulatedFrames;

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
				sceneData.pMeshInstanceBuffer->GetSRV()->GetDescriptor(),
			};

			const D3D12_CPU_DESCRIPTOR_HANDLE uavs[] = {
				sceneData.pRenderTarget->GetUAV()->GetDescriptor(),
				m_pAccumulationTexture->GetUAV()->GetDescriptor(),
			};

			context.SetComputeDynamicConstantBufferView(0, parameters);
			context.SetComputeDynamicConstantBufferView(1, *sceneData.pShadowData);
			context.BindResources(2, 0, uavs, ARRAYSIZE(uavs));
			context.BindResources(3, 0, srvs, ARRAYSIZE(srvs));
			context.BindResourceTable(4, sceneData.GlobalSRVHeapHandle.GpuHandle, CommandListContext::Compute);

			context.DispatchRays(bindingTable, sceneData.pRenderTarget->GetWidth(), sceneData.pRenderTarget->GetHeight());
		});
}

void PathTracing::OnResize(uint32 width, uint32 height)
{
	if (!IsSupported())
	{
		return;
	}
	m_pAccumulationTexture = m_pDevice->CreateTexture(TextureDesc::Create2D(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, TextureFlag::UnorderedAccess), "Accumulation Target");
}

void PathTracing::Reset()
{
	m_NumAccumulatedFrames = 0;
}

bool PathTracing::IsSupported()
{
	return m_pDevice->GetCapabilities().SupportsRaytracing();
}
