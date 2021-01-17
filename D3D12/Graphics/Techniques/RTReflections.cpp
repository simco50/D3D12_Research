#include "stdafx.h"
#include "RTReflections.h"
#include "Graphics/Core/Shader.h"
#include "Graphics/Core/PipelineState.h"
#include "Graphics/Core/RootSignature.h"
#include "Graphics/Core/GraphicsBuffer.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/Core/CommandContext.h"
#include "Graphics/Core/Texture.h"
#include "Graphics/Core/RaytracingCommon.h"
#include "Graphics/Core/ResourceViews.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/Mesh.h"
#include "Scene/Camera.h"

RTReflections::RTReflections(Graphics* pGraphics)
{
	if (pGraphics->SupportsRayTracing())
	{
		SetupResources(pGraphics);
		SetupPipelines(pGraphics);
	}
}

void RTReflections::Execute(RGGraph& graph, const SceneData& sceneData)
{
	RGPassBuilder rt = graph.AddPass("RT Reflections");
	rt.Bind([=](CommandContext& context, const RGPassResources& passResources)
		{
			context.CopyTexture(sceneData.pResolvedTarget, m_pSceneColor.get());

			context.InsertResourceBarrier(sceneData.pResolvedDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(sceneData.pResolvedNormals, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pSceneColor.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(sceneData.pResolvedTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			context.SetComputeRootSignature(m_pGlobalRS.get());
			context.SetPipelineState(m_pRtSO.Get());

			struct Parameters
			{
				Matrix ViewInverse;
				Matrix ProjectionInverse;
				uint32 NumLights;
				float ViewPixelSpreadAngle;
			} parameters{};

			parameters.ViewInverse = sceneData.pCamera->GetViewInverse();
			parameters.ProjectionInverse = sceneData.pCamera->GetProjectionInverse();
			parameters.NumLights = sceneData.pLightBuffer->GetDesc().ElementCount;
			parameters.ViewPixelSpreadAngle = atanf(2.0f * tanf(sceneData.pCamera->GetFoV() / 2) / (float)m_pSceneColor->GetHeight());

			ShaderBindingTable bindingTable(m_pRtSO.Get());
			bindingTable.BindRayGenShader("RayGen");
			bindingTable.BindMissShader("Miss", 0);
			bindingTable.BindMissShader("ShadowMiss", 1);

			for (int i = 0; i < sceneData.pMesh->GetMeshCount(); ++i)
			{
				SubMesh* pMesh = sceneData.pMesh->GetMesh(i);
				auto it = std::find_if(sceneData.Batches.begin(), sceneData.Batches.end(), [pMesh](const Batch& b) { return b.pMesh == pMesh; });
				struct HitData
				{
					MaterialData Material;
					uint32 VertexBufferOffset;
					uint32 IndexBufferOffset;
				} hitData;
				hitData.Material = it->Material;
				hitData.VertexBufferOffset = (uint32)(pMesh->GetVertexBuffer().Location - sceneData.pMesh->GetData()->GetGpuHandle());
				hitData.IndexBufferOffset = (uint32)(pMesh->GetIndexBuffer().Location - sceneData.pMesh->GetData()->GetGpuHandle());

				DynamicAllocation allocation = context.AllocateTransientMemory(sizeof(HitData));
				memcpy(allocation.pMappedMemory, &hitData, sizeof(HitData));
				bindingTable.BindHitGroup("ReflectionHitGroup", { allocation.GpuHandle });
			}

			const D3D12_CPU_DESCRIPTOR_HANDLE srvs[] = {
				sceneData.pLightBuffer->GetSRV()->GetDescriptor(),
				sceneData.pLightBuffer->GetSRV()->GetDescriptor() /*dummy*/,
				sceneData.pResolvedDepth->GetSRV(),
				m_pSceneColor->GetSRV(),
				sceneData.pResolvedNormals->GetSRV(),
				sceneData.pMesh->GetData()->GetSRV()->GetDescriptor(),
			};

			context.SetComputeDynamicConstantBufferView(0, &parameters, sizeof(Parameters));
			context.SetDynamicDescriptor(1, 0, sceneData.pResolvedTarget->GetUAV());
			context.SetDynamicDescriptors(2, 0, srvs, ARRAYSIZE(srvs));
			context.SetDynamicDescriptor(3, 0, sceneData.pTLAS->GetSRV()->GetDescriptor());
			context.SetDynamicDescriptors(4, 0, sceneData.MaterialTextures.data(), (int)sceneData.MaterialTextures.size());

			context.DispatchRays(bindingTable, sceneData.pResolvedTarget->GetWidth(), sceneData.pResolvedTarget->GetHeight());
		});
}

void RTReflections::OnResize(uint32 width, uint32 height)
{
	m_pSceneColor->Create(TextureDesc::Create2D(width, height, Graphics::RENDER_TARGET_FORMAT, TextureFlag::ShaderResource, 1, 1));
}

void RTReflections::SetupResources(Graphics* pGraphics)
{
	m_pSceneColor = std::make_unique<Texture>(pGraphics);
}

void RTReflections::SetupPipelines(Graphics* pGraphics)
{
	//Raytracing Pipeline
	{
		ShaderLibrary shaderLibrary("RTReflections.hlsl");

		m_pRayGenSignature = std::make_unique<RootSignature>(pGraphics);
		m_pRayGenSignature->Finalize("Ray Gen", D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);

		m_pHitSignature = std::make_unique<RootSignature>(pGraphics);
		m_pHitSignature->SetConstantBufferView(0, 1, D3D12_SHADER_VISIBILITY_ALL);
		m_pHitSignature->SetShaderResourceView(1, 100, D3D12_SHADER_VISIBILITY_ALL);
		m_pHitSignature->SetShaderResourceView(2, 101, D3D12_SHADER_VISIBILITY_ALL);
		m_pHitSignature->Finalize("Hit", D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);

		m_pMissSignature = std::make_unique<RootSignature>(pGraphics);
		m_pMissSignature->Finalize("Miss", D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);

		m_pGlobalRS = std::make_unique<RootSignature>(pGraphics);
		m_pGlobalRS->FinalizeFromShader("Global RS", shaderLibrary);

		CD3DX12_STATE_OBJECT_HELPER stateDesc;
		const char* pLibraryExports[] = {
			"RayGen", "ClosestHit", "Miss", "ShadowMiss"
		};
		stateDesc.AddLibrary(CD3DX12_SHADER_BYTECODE(shaderLibrary.GetByteCode(), shaderLibrary.GetByteCodeSize()), pLibraryExports, ARRAYSIZE(pLibraryExports));
		stateDesc.AddHitGroup("ReflectionHitGroup", "ClosestHit");
		stateDesc.BindLocalRootSignature("RayGen", m_pRayGenSignature->GetRootSignature());
		stateDesc.BindLocalRootSignature("Miss", m_pMissSignature->GetRootSignature());
		stateDesc.BindLocalRootSignature("ShadowMiss", m_pMissSignature->GetRootSignature());
		stateDesc.BindLocalRootSignature("ReflectionHitGroup", m_pHitSignature->GetRootSignature());
		stateDesc.SetRaytracingShaderConfig(5 * sizeof(float), 2 * sizeof(float));
		stateDesc.SetRaytracingPipelineConfig(2);
		stateDesc.SetGlobalRootSignature(m_pGlobalRS->GetRootSignature());
		D3D12_STATE_OBJECT_DESC desc = stateDesc.Desc();
		pGraphics->GetRaytracingDevice()->CreateStateObject(&desc, IID_PPV_ARGS(m_pRtSO.GetAddressOf()));
	}
}
