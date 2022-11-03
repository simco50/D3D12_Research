#include "stdafx.h"
#include "DDGI.h"

#include "../RHI/Graphics.h"
#include "../RHI/RootSignature.h"
#include "../RHI/PipelineState.h"
#include "../RHI/StateObject.h"
#include "../RHI/ShaderBindingTable.h"
#include "../RenderGraph/RenderGraph.h"
#include "../SceneView.h"

DDGI::DDGI(GraphicsDevice* pDevice)
{
	if (pDevice->GetCapabilities().SupportsRaytracing())
	{
		m_pCommonRS = new RootSignature(pDevice);
		m_pCommonRS->AddRootConstants(0, 8);
		m_pCommonRS->AddConstantBufferView(100);
		m_pCommonRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 6);
		m_pCommonRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 6);
		m_pCommonRS->Finalize("Common");

		m_pDDGIUpdateIrradianceColorPSO = pDevice->CreateComputePipeline(m_pCommonRS, "RayTracing/DDGI.hlsl", "UpdateIrradianceCS");
		m_pDDGIUpdateIrradianceDepthPSO = pDevice->CreateComputePipeline(m_pCommonRS, "RayTracing/DDGI.hlsl", "UpdateDepthCS");
		m_pDDGIUpdateProbeStatesPSO = pDevice->CreateComputePipeline(m_pCommonRS, "RayTracing/DDGI.hlsl", "UpdateProbeStatesCS");

		StateObjectInitializer soDesc{};
		soDesc.Name = "DDGI Trace Rays";
		soDesc.MaxRecursion = 1;
		soDesc.MaxPayloadSize = 6 * sizeof(float);
		soDesc.MaxAttributeSize = 2 * sizeof(float);
		soDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
		soDesc.AddLibrary("RayTracing/DDGIRayTrace.hlsl", { "TraceRaysRGS" });
		soDesc.AddLibrary("RayTracing/SharedRaytracingLib.hlsl", { "OcclusionMS", "MaterialCHS", "MaterialAHS", "MaterialMS" });
		soDesc.AddHitGroup("MaterialHG", "MaterialCHS", "MaterialAHS");
		soDesc.AddMissShader("MaterialMS");
		soDesc.AddMissShader("OcclusionMiss");
		soDesc.pGlobalRootSignature = m_pCommonRS;
		m_pDDGITraceRaysSO = pDevice->CreateStateObject(soDesc);

		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pCommonRS);
		psoDesc.SetVertexShader("RayTracing/DDGI.hlsl", "VisualizeIrradianceVS");
		psoDesc.SetPixelShader("RayTracing/DDGI.hlsl", "VisualizeIrradiancePS");
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		psoDesc.SetRenderTargetFormats(ResourceFormat::RGBA8_UNORM, ResourceFormat::D32_FLOAT, 1);
		psoDesc.SetName("Visualize Irradiance");
		psoDesc.SetCullMode(D3D12_CULL_MODE_NONE);
		m_pDDGIVisualizePSO = pDevice->CreatePipeline(psoDesc);
	}
}

void DDGI::Execute(RGGraph& graph, const SceneView* pView, World* pWorld)
{
	if (pWorld->DDGIVolumes.size() > 0 && m_pDDGITraceRaysSO)
	{
		RG_GRAPH_SCOPE("DDGI", graph);

		uint32 randomIndex = Math::RandomRange(0, (int)pWorld->DDGIVolumes.size() - 1);
		DDGIVolume& ddgi = pWorld->DDGIVolumes[randomIndex];

		struct
		{
			Vector3 RandomVector;
			float RandomAngle;
			float HistoryBlendWeight;
			uint32 VolumeIndex;
		} parameters;

		parameters.RandomVector = Math::RandVector();
		parameters.RandomAngle = Math::RandomRange(0.0f, 2.0f * Math::PI);
		parameters.HistoryBlendWeight = 0.98f;
		parameters.VolumeIndex = randomIndex;

		const uint32 numProbes = ddgi.NumProbes.x * ddgi.NumProbes.y * ddgi.NumProbes.z;

		// Must match with shader!
		constexpr uint32 probeIrradianceTexels = 6;
		constexpr uint32 probeDepthTexel = 14;
		auto ProbeTextureDimensions = [](const Vector3i& numProbes, uint32 texelsPerProbe) {
			uint32 width = (1 + texelsPerProbe + 1) * numProbes.y * numProbes.x;
			uint32 height = (1 + texelsPerProbe + 1) * numProbes.z;
			return Vector2i(width, height);
		};

		Vector2i ddgiIrradianceDimensions = ProbeTextureDimensions(ddgi.NumProbes, probeIrradianceTexels);
		TextureDesc ddgiIrradianceDesc = TextureDesc::Create2D(ddgiIrradianceDimensions.x, ddgiIrradianceDimensions.y, ResourceFormat::RGBA16_FLOAT, TextureFlag::UnorderedAccess);
		RGTexture* pIrradianceTarget = graph.CreateTexture("DDGI Irradiance Target", ddgiIrradianceDesc);
		RGTexture* pIrradianceHistory = RGUtils::CreatePersistentTexture(graph, "DDGI Irradiance History", ddgiIrradianceDesc, &ddgi.pIrradianceHistory, false);
		graph.ExportTexture(pIrradianceTarget, &ddgi.pIrradianceHistory);

		Vector2i ddgiDepthDimensions = ProbeTextureDimensions(ddgi.NumProbes, probeDepthTexel);
		TextureDesc ddgiDepthDesc = TextureDesc::Create2D(ddgiDepthDimensions.x, ddgiDepthDimensions.y, ResourceFormat::RG16_FLOAT, TextureFlag::UnorderedAccess);
		RGTexture* pDepthTarget = graph.CreateTexture("DDGI Depth Target", ddgiDepthDesc);
		RGTexture* pDepthHistory = RGUtils::CreatePersistentTexture(graph, "DDGI Depth History", ddgiDepthDesc, &ddgi.pDepthHistory, false);
		graph.ExportTexture(pDepthTarget, &ddgi.pDepthHistory);

		RGBuffer* pRayBuffer = graph.CreateBuffer("DDGI Ray Buffer", BufferDesc::CreateTyped(numProbes * ddgi.MaxNumRays, ResourceFormat::RGBA16_FLOAT));
		RGBuffer* pProbeStates = RGUtils::CreatePersistentBuffer(graph, "DDGI States Buffer", BufferDesc::CreateTyped(numProbes, ResourceFormat::R8_UINT), &ddgi.pProbeStates, true);
		RGBuffer* pProbeOffsets = RGUtils::CreatePersistentBuffer(graph, "DDGI Probe Offsets", BufferDesc::CreateTyped(numProbes, ResourceFormat::RGBA16_FLOAT), &ddgi.pProbeOffset, true);

		graph.AddPass("DDGI Raytrace", RGPassFlag::Compute)
			.Read(pProbeStates)
			.Write(pRayBuffer)
			.Bind([=](CommandContext& context)
				{
					context.SetComputeRootSignature(m_pCommonRS);
					context.SetPipelineState(m_pDDGITraceRaysSO);

					context.SetRootConstants(0, parameters);
					context.SetRootCBV(1, Renderer::GetViewUniforms(pView));
					context.BindResources(2, pRayBuffer->Get()->GetUAV());

					ShaderBindingTable bindingTable(m_pDDGITraceRaysSO);
					bindingTable.BindRayGenShader("TraceRaysRGS");
					bindingTable.BindMissShader("MaterialMS", 0);
					bindingTable.BindMissShader("OcclusionMS", 1);
					bindingTable.BindHitGroup("MaterialHG", 0);

					context.DispatchRays(bindingTable, ddgi.NumRays, numProbes);
				});

		graph.AddPass("DDGI Update Irradiance", RGPassFlag::Compute)
			.Read({ pIrradianceHistory, pRayBuffer, pProbeStates })
			.Write(pIrradianceTarget)
			.Bind([=](CommandContext& context)
				{
					context.SetComputeRootSignature(m_pCommonRS);
					context.SetPipelineState(m_pDDGIUpdateIrradianceColorPSO);

					context.SetRootConstants(0, parameters);
					context.SetRootCBV(1, Renderer::GetViewUniforms(pView));
					context.BindResources(2, pIrradianceTarget->Get()->GetUAV());
					context.BindResources(3, {
						pRayBuffer->Get()->GetSRV(),
						});

					context.Dispatch(numProbes);
				});

		graph.AddPass("DDGI Update Depth", RGPassFlag::Compute)
			.Read({ pDepthHistory, pRayBuffer, pProbeStates })
			.Write(pDepthTarget)
			.Bind([=](CommandContext& context)
				{
					context.SetComputeRootSignature(m_pCommonRS);
					context.SetPipelineState(m_pDDGIUpdateIrradianceDepthPSO);

					context.SetRootConstants(0, parameters);
					context.SetRootCBV(1, Renderer::GetViewUniforms(pView));
					context.BindResources(2, {
						pDepthTarget->Get()->GetUAV(),
						});
					context.BindResources(3, {
						pRayBuffer->Get()->GetSRV(),
						});

					context.Dispatch(numProbes);
				});

		graph.AddPass("DDGI Update Probe States", RGPassFlag::Compute)
			.Read(pRayBuffer)
			.Write({ pProbeOffsets, pProbeStates })
			.Bind([=](CommandContext& context)
				{
					context.SetComputeRootSignature(m_pCommonRS);
					context.SetPipelineState(m_pDDGIUpdateProbeStatesPSO);

					context.SetRootConstants(0, parameters);
					context.SetRootCBV(1, Renderer::GetViewUniforms(pView));
					context.BindResources(2, {
						pProbeStates->Get()->GetUAV(),
						pProbeOffsets->Get()->GetUAV(),
						});
					context.BindResources(3, {
						pRayBuffer->Get()->GetSRV(),
						});

					context.Dispatch(ComputeUtils::GetNumThreadGroups(numProbes, 32));
				});

		graph.AddPass("Bindless Transition", RGPassFlag::NeverCull | RGPassFlag::Raster)
			.Read({ pDepthTarget, pIrradianceTarget, pProbeStates, pProbeOffsets });
	}
}

void DDGI::RenderVisualization(RGGraph& graph, const SceneView* pView, const World* pWorld, const SceneTextures& sceneTextures)
{
	for (uint32 i = 0; i < pWorld->DDGIVolumes.size(); ++i)
	{
		const DDGIVolume& ddgi = pWorld->DDGIVolumes[i];
		graph.AddPass("DDGI Visualize", RGPassFlag::Raster)
			.DepthStencil(sceneTextures.pDepth, RenderTargetLoadAction::Load, true)
			.RenderTarget(sceneTextures.pColorTarget, RenderTargetLoadAction::Load)
			.Bind([=](CommandContext& context)
				{
					context.SetGraphicsRootSignature(m_pCommonRS);
					context.SetPipelineState(m_pDDGIVisualizePSO);
					context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

					struct
					{
						uint32 VolumeIndex;
					} parameters;
					parameters.VolumeIndex = i;

					context.SetRootConstants(0, parameters);
					context.SetRootCBV(1, Renderer::GetViewUniforms(pView));
					context.Draw(0, 2880, ddgi.NumProbes.x * ddgi.NumProbes.y * ddgi.NumProbes.z);
				});
	}
}
