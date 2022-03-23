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

PathTracing::PathTracing(GraphicsDevice* pDevice)
	: m_pDevice(pDevice)
{
	if (!IsSupported())
	{
		return;
	}

	m_pRS = new RootSignature(pDevice);
	m_pRS->AddConstantBufferView(0);
	m_pRS->AddConstantBufferView(100);
	m_pRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2);
	m_pRS->Finalize("Global");

	StateObjectInitializer desc{};
	desc.Name = "Path Tracing";
	desc.MaxRecursion = 1;
	desc.MaxPayloadSize = 14 * sizeof(float);
	desc.MaxAttributeSize = 2 * sizeof(float);
	desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
	desc.AddLibrary("PathTracing.hlsl");
	desc.AddLibrary("CommonRaytracingLib.hlsl");
	desc.AddHitGroup("PrimaryHG", "PrimaryCHS", "PrimaryAHS");
	desc.AddMissShader("PrimaryMS");
	desc.AddMissShader("OcclusionMiss");
	desc.pGlobalRootSignature = m_pRS;
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

void PathTracing::Render(RGGraph& graph, const SceneView& sceneData, Texture* pTarget)
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
	}
	ImGui::End();

	if (sceneData.View.PreviousViewProjection != sceneData.View.ViewProjection)
	{
		Reset();
	}

	m_NumAccumulatedFrames++;

	RGPassBuilder rt = graph.AddPass("Path Tracing");
	rt.Bind([=](CommandContext& context, const RGPassResources& /* passResources */)
		{
			context.InsertResourceBarrier(pTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			context.SetComputeRootSignature(m_pRS);
			context.SetPipelineState(m_pSO);

			struct
			{
				uint32 NumBounces;
				uint32 AccumulatedFrames;
			} parameters;

			parameters.NumBounces = numBounces;
			parameters.AccumulatedFrames = m_NumAccumulatedFrames;

			ShaderBindingTable bindingTable(m_pSO);
			bindingTable.BindRayGenShader("RayGen");
			bindingTable.BindMissShader("PrimaryMS", 0);
			bindingTable.BindMissShader("OcclusionMiss", 1);
			bindingTable.BindHitGroup("PrimaryHG", 0);

			context.SetRootCBV(0, parameters);
			context.SetRootCBV(1, GetViewUniforms(sceneData, pTarget));
			context.BindResources(2, {
				pTarget->GetUAV(),
				m_pAccumulationTexture->GetUAV(),
				});

			context.DispatchRays(bindingTable, pTarget->GetWidth(), pTarget->GetHeight());
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
