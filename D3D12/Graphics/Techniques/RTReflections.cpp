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
			parameters.NumLights = sceneData.pLightBuffer->GetNumElements();
			parameters.ViewPixelSpreadAngle = atanf(2.0f * tanf(sceneData.pCamera->GetFoV() / 2) / (float)m_pSceneColor->GetHeight());

			ShaderBindingTable bindingTable(m_pRtSO.Get());
			bindingTable.BindRayGenShader("RayGen");
			bindingTable.BindMissShader("Miss", 0);
			bindingTable.BindMissShader("ShadowMiss", 1);

			std::vector<Batch> sortedBatches = sceneData.Batches;
			std::sort(sortedBatches.begin(), sortedBatches.end(), [](const Batch& a, const Batch& b) { return a.Index < b.Index; });

			for (const Batch& b : sortedBatches)
			{
				struct HitData
				{
					MaterialData Material;
				} hitData;
				hitData.Material = b.Material;

				DynamicAllocation allocation = context.AllocateTransientMemory(sizeof(HitData));
				memcpy(allocation.pMappedMemory, &hitData, sizeof(HitData));

				std::vector<uint64> handles = {
					allocation.GpuHandle,
					b.pMesh->GetVertexBuffer().Location,
					b.pMesh->GetIndexBuffer().Location,
				};

				bindingTable.BindHitGroup("ReflectionHitGroup", handles);
			}

			const D3D12_CPU_DESCRIPTOR_HANDLE srvs[] = {
				sceneData.pLightBuffer->GetSRV()->GetDescriptor(),
				sceneData.pLightBuffer->GetSRV()->GetDescriptor() /*dummy*/,
				sceneData.pResolvedDepth->GetSRV(),
				m_pSceneColor->GetSRV(),
				sceneData.pResolvedNormals->GetSRV(),
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
	ShaderLibrary* pShaderLibrary = pGraphics->GetShaderManager()->GetLibrary("RTReflections.hlsl");

	m_pHitSignature = std::make_unique<RootSignature>(pGraphics);
	m_pHitSignature->SetConstantBufferView(0, 1, D3D12_SHADER_VISIBILITY_ALL);
	m_pHitSignature->SetShaderResourceView(1, 0, D3D12_SHADER_VISIBILITY_ALL);
	m_pHitSignature->SetShaderResourceView(2, 1, D3D12_SHADER_VISIBILITY_ALL);
	m_pHitSignature->Finalize("Hit", D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);

	m_pGlobalRS = std::make_unique<RootSignature>(pGraphics);
	m_pGlobalRS->FinalizeFromShader("Global", pShaderLibrary);

	CD3DX12_STATE_OBJECT_HELPER stateDesc;
	const char* pLibraryExports[] = {
		"RayGen", "ClosestHit", "Miss", "ShadowMiss"
	};
	stateDesc.AddLibrary(CD3DX12_SHADER_BYTECODE(pShaderLibrary->GetByteCode(), pShaderLibrary->GetByteCodeSize()), pLibraryExports, ARRAYSIZE(pLibraryExports));
	stateDesc.AddHitGroup("ReflectionHitGroup", "ClosestHit");
	stateDesc.BindLocalRootSignature("ReflectionHitGroup", m_pHitSignature->GetRootSignature());
	stateDesc.SetRaytracingShaderConfig(5 * sizeof(float), 2 * sizeof(float));
	stateDesc.SetRaytracingPipelineConfig(2);
	stateDesc.SetGlobalRootSignature(m_pGlobalRS->GetRootSignature());
	D3D12_STATE_OBJECT_DESC desc = stateDesc.Desc();
	pGraphics->GetRaytracingDevice()->CreateStateObject(&desc, IID_PPV_ARGS(m_pRtSO.GetAddressOf()));
}
