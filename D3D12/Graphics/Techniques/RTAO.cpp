#include "stdafx.h"
#include "RTAO.h"
#include "Graphics/Core/Shader.h"
#include "Graphics/Core/PipelineState.h"
#include "Graphics/Core/RootSignature.h"
#include "Graphics/Core/GraphicsBuffer.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/Core/CommandContext.h"
#include "Graphics/Core/Texture.h"
#include "Graphics/Core/ShaderBindingTable.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/Mesh.h"
#include "Scene/Camera.h"
#include "../Core/StateObject.h"

RTAO::RTAO(Graphics* pGraphics)
{
	if (pGraphics->SupportsRayTracing())
	{
		SetupResources(pGraphics);
		SetupPipelines(pGraphics);
	}
}

void RTAO::Execute(RGGraph& graph, Texture* pColor, Texture* pDepth, Buffer* pTLAS, Camera& camera)
{
	static float g_AoPower = 3;
	static float g_AoRadius = 0.5f;
	static int32 g_AoSamples = 1;

	ImGui::Begin("Parameters");
	ImGui::Text("Ambient Occlusion");
	ImGui::SliderFloat("Power", &g_AoPower, 0, 10);
	ImGui::SliderFloat("Radius", &g_AoRadius, 0.1f, 5.0f);
	ImGui::SliderInt("Samples", &g_AoSamples, 1, 64);
	ImGui::End();

	RGPassBuilder rt = graph.AddPass("RTAO");
	rt.Bind([=](CommandContext& context, const RGPassResources& passResources)
		{
			context.InsertResourceBarrier(pDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(pColor, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			context.SetComputeRootSignature(m_pGlobalRS.get());
			context.SetPipelineState(m_pRtSO);

			constexpr const int numRandomVectors = 64;
			struct Parameters
			{
				Matrix ViewInverse;
				Matrix ProjectionInverse;
				Vector4 RandomVectors[numRandomVectors];
				float Power;
				float Radius;
				int32 Samples;
			} parameters{};

			static bool written = false;
			static Vector4 randoms[numRandomVectors];
			if (!written)
			{
				srand(2);
				written = true;
				for (int i = 0; i < numRandomVectors; ++i)
				{
					randoms[i] = Vector4(Math::RandVector());
					randoms[i].z = Math::Lerp(0.1f, 0.8f, (float)abs(randoms[i].z));
					randoms[i].Normalize();
					randoms[i] *= Math::Lerp(0.1f, 1.0f, (float)pow(Math::RandomRange(0, 1), 2));
				}
			}
			memcpy(parameters.RandomVectors, randoms, sizeof(Vector4) * numRandomVectors);

			parameters.ViewInverse = camera.GetViewInverse();
			parameters.ProjectionInverse = camera.GetProjectionInverse();
			parameters.Power = g_AoPower;
			parameters.Radius = g_AoRadius;
			parameters.Samples = g_AoSamples;

			ShaderBindingTable bindingTable(m_pRtSO);
			bindingTable.BindRayGenShader("RayGen");
			bindingTable.BindMissShader("Miss", {});

			context.SetComputeDynamicConstantBufferView(0, &parameters, sizeof(Parameters));
			context.SetDynamicDescriptor(1, 0, pColor->GetUAV());
			context.SetDynamicDescriptor(2, 0, pTLAS->GetSRV());
			context.SetDynamicDescriptor(2, 1, pDepth->GetSRV());

			context.DispatchRays(bindingTable, pColor->GetWidth(), pColor->GetHeight());
		});
}

void RTAO::SetupResources(Graphics* pGraphics)
{
}

void RTAO::SetupPipelines(Graphics* pGraphics)
{
	m_pGlobalRS = std::make_unique<RootSignature>(pGraphics);
	m_pGlobalRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
	m_pGlobalRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, D3D12_SHADER_VISIBILITY_ALL);
	m_pGlobalRS->SetDescriptorTableSimple(2, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, D3D12_SHADER_VISIBILITY_ALL);
	m_pGlobalRS->AddStaticSampler(0, CD3DX12_STATIC_SAMPLER_DESC(0, D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP), D3D12_SHADER_VISIBILITY_ALL);
	m_pGlobalRS->Finalize("Global", D3D12_ROOT_SIGNATURE_FLAG_NONE);

	ShaderLibrary* pShaderLibrary = pGraphics->GetShaderManager()->GetLibrary("RTAO.hlsl");

	StateObjectInitializer stateDesc;
	stateDesc.AddLibrary(pShaderLibrary, { "RayGen", "Miss" });
	stateDesc.Name = "RT AO";
	stateDesc.MaxPayloadSize = sizeof(float);
	stateDesc.MaxAttributeSize = 2 * sizeof(float);
	stateDesc.pGlobalRootSignature = m_pGlobalRS.get();
	stateDesc.RayGenShader = "RayGen";
	stateDesc.AddMissShader("Miss");
	m_pRtSO = pGraphics->CreateStateObject(stateDesc);
}
