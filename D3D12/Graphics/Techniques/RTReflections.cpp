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

Texture* RTReflections::Execute(RGGraph& graph, const SceneData& sceneData)
{
	RGPassBuilder rt = graph.AddPass("RT Reflections");
	rt.Bind([=](CommandContext& context, const RGPassResources& passResources)
		{
			context.InsertResourceBarrier(sceneData.pResolvedDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pReflections.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			context.SetComputeRootSignature(m_pGlobalRS.get());
			context.SetPipelineState(m_pRtSO.Get());

			struct Parameters
			{
				Matrix ViewInverse;
				Matrix ViewProjectionInverse;
			} parameters{};

			parameters.ViewInverse = sceneData.pCamera->GetViewInverse();
			parameters.ViewProjectionInverse = sceneData.pCamera->GetProjectionInverse() * sceneData.pCamera->GetViewInverse();

			ShaderBindingTable bindingTable(m_pRtSO.Get());
			bindingTable.AddRayGenEntry("RayGen", {});
			bindingTable.AddMissEntry("Miss", {});
			bindingTable.AddMissEntry("ShadowMiss", {});

			for (int i = 0; i < sceneData.pMesh->GetMeshCount(); ++i)
			{
				SubMesh* pMesh = sceneData.pMesh->GetMesh(i);
				if (sceneData.pMesh->GetMaterial(pMesh->GetMaterialId()).IsTransparent)
				{
					continue;
				}

				auto it = std::find_if(sceneData.OpaqueBatches.begin(), sceneData.OpaqueBatches.end(), [pMesh](const Batch& b) { return b.pMesh == pMesh; });
				const MaterialData& material = it->Material;
				DynamicAllocation allocation = context.AllocateTransientMemory(sizeof(MaterialData));
				memcpy(allocation.pMappedMemory, &material, sizeof(MaterialData));
				bindingTable.AddHitGroupEntry("HitGroup", {allocation.GpuHandle, pMesh->GetVertexBuffer().Location, pMesh->GetIndexBuffer().Location });
				bindingTable.AddHitGroupEntry("ShadowHitGroup", { });
			}


			context.SetComputeDynamicConstantBufferView(0, &parameters, sizeof(Parameters));
			context.SetDynamicDescriptor(1, 0, m_pReflections->GetUAV());
			context.SetDynamicDescriptor(2, 0, sceneData.pTLAS->GetSRV());
			context.SetDynamicDescriptor(2, 1, sceneData.pResolvedDepth->GetSRV());
			context.SetDynamicDescriptor(2, 2, sceneData.pLightBuffer->GetSRV());
			context.SetDynamicDescriptors(3, 0, sceneData.MaterialTextures.data(), (int)sceneData.MaterialTextures.size());

			context.DispatchRays(bindingTable, m_pReflections->GetWidth(), m_pReflections->GetHeight());
		});

	return m_pReflections.get();
}

void RTReflections::SetupResources(Graphics* pGraphics)
{
	m_pReflections = std::make_unique<Texture>(pGraphics);
	m_pReflections->Create(TextureDesc::Create2D(1920, 1080, DXGI_FORMAT_R8G8B8A8_UNORM, TextureFlag::UnorderedAccess | TextureFlag::ShaderResource, 1, 1));
}

void RTReflections::SetupPipelines(Graphics* pGraphics)
{
	//Raytracing Pipeline
	{
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
		m_pGlobalRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
		m_pGlobalRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, D3D12_SHADER_VISIBILITY_ALL);
		m_pGlobalRS->SetDescriptorTableSimple(2, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, D3D12_SHADER_VISIBILITY_ALL);
		m_pGlobalRS->SetDescriptorTableSimple(3, 200, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 128, D3D12_SHADER_VISIBILITY_ALL);
		m_pGlobalRS->AddStaticSampler(0, CD3DX12_STATIC_SAMPLER_DESC(0, D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT), D3D12_SHADER_VISIBILITY_ALL);
		m_pGlobalRS->Finalize("Dummy Global", D3D12_ROOT_SIGNATURE_FLAG_NONE);

		ShaderLibrary shaderLibrary("RTReflections.hlsl");

		CD3DX12_STATE_OBJECT_HELPER stateDesc;
		stateDesc.AddLibrary(CD3DX12_SHADER_BYTECODE(shaderLibrary.GetByteCode(), shaderLibrary.GetByteCodeSize()), { "RayGen", "ClosestHit", "Miss", "ShadowClosestHit", "ShadowMiss" });
		stateDesc.AddHitGroup("HitGroup", "ClosestHit");
		stateDesc.AddHitGroup("ShadowHitGroup", "ShadowClosestHit");
		stateDesc.BindLocalRootSignature("RayGen", m_pRayGenSignature->GetRootSignature());
		stateDesc.BindLocalRootSignature("Miss", m_pMissSignature->GetRootSignature());
		stateDesc.BindLocalRootSignature("ShadowMiss", m_pMissSignature->GetRootSignature());
		stateDesc.BindLocalRootSignature("HitGroup", m_pHitSignature->GetRootSignature());
		stateDesc.BindLocalRootSignature("ShadowHitGroup", m_pMissSignature->GetRootSignature());
		stateDesc.SetRaytracingShaderConfig(3 * sizeof(float), 2 * sizeof(float));
		stateDesc.SetRaytracingPipelineConfig(2);
		stateDesc.SetGlobalRootSignature(m_pGlobalRS->GetRootSignature());
		D3D12_STATE_OBJECT_DESC desc = stateDesc.Desc();
		pGraphics->GetRaytracingDevice()->CreateStateObject(&desc, IID_PPV_ARGS(m_pRtSO.GetAddressOf()));
	}
}
