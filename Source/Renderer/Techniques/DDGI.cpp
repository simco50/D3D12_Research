#include "stdafx.h"
#include "DDGI.h"

#include "RHI/Device.h"
#include "RHI/StateObject.h"
#include "RHI/RootSignature.h"
#include "RHI/PipelineState.h"
#include "RHI/ShaderBindingTable.h"
#include "RenderGraph/RenderGraph.h"
#include "Renderer/Renderer.h"
#include "Scene/World.h"

DDGI::DDGI(GraphicsDevice* pDevice)
{
	if (pDevice->GetCapabilities().SupportsRaytracing())
	{
		m_pDDGIUpdateIrradianceColorPSO = pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "RayTracing/DDGI.hlsl", "UpdateIrradianceCS");
		m_pDDGIUpdateIrradianceDepthPSO = pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "RayTracing/DDGI.hlsl", "UpdateDepthCS");
		m_pDDGIUpdateProbeStatesPSO = pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "RayTracing/DDGI.hlsl", "UpdateProbeStatesCS");

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
		soDesc.pGlobalRootSignature = GraphicsCommon::pCommonRS;
		m_pDDGITraceRaysSO = pDevice->CreateStateObject(soDesc);

		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(GraphicsCommon::pCommonRS);
		psoDesc.SetVertexShader("RayTracing/DDGI.hlsl", "VisualizeIrradianceVS");
		psoDesc.SetPixelShader("RayTracing/DDGI.hlsl", "VisualizeIrradiancePS");
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		psoDesc.SetRenderTargetFormats(ResourceFormat::RGBA16_FLOAT, Renderer::DepthStencilFormat, 1);
		psoDesc.SetName("Visualize Irradiance");
		psoDesc.SetCullMode(D3D12_CULL_MODE_NONE);
		m_pDDGIVisualizePSO = pDevice->CreatePipeline(psoDesc);
	}
}

DDGI::~DDGI()
{
}

void DDGI::Execute(RGGraph& graph, const RenderView* pView)
{
	if (m_pDDGITraceRaysSO)
	{
		RG_GRAPH_SCOPE("DDGI", graph);
		auto ddgi_view = pView->pWorld->Registry.view<DDGIVolume>();

		uint32 i = 0;
		uint32 randomIndex = Math::RandomRange(0, (int)ddgi_view.size() - 1);
		ddgi_view.each([&](DDGIVolume& ddgi)
			{
				if (randomIndex != i)
					return;
				++i;

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
				TextureDesc ddgiIrradianceDesc = TextureDesc::Create2D(ddgiIrradianceDimensions.x, ddgiIrradianceDimensions.y, ResourceFormat::RGBA16_FLOAT);
				RGTexture* pIrradianceTarget = graph.Create("DDGI Irradiance Target", ddgiIrradianceDesc);
				RGTexture* pIrradianceHistory = graph.TryImport(ddgi.pIrradianceHistory, GraphicsCommon::GetDefaultTexture(DefaultTexture::Black2D));
				graph.Export(pIrradianceTarget, &ddgi.pIrradianceHistory);

				Vector2i ddgiDepthDimensions = ProbeTextureDimensions(ddgi.NumProbes, probeDepthTexel);
				TextureDesc ddgiDepthDesc = TextureDesc::Create2D(ddgiDepthDimensions.x, ddgiDepthDimensions.y, ResourceFormat::RG16_FLOAT);
				RGTexture* pDepthTarget = graph.Create("DDGI Depth Target", ddgiDepthDesc);
				RGTexture* pDepthHistory = graph.TryImport(ddgi.pDepthHistory, GraphicsCommon::GetDefaultTexture(DefaultTexture::Black2D));
				graph.Export(pDepthTarget, &ddgi.pDepthHistory);

				RGBuffer* pRayBuffer = graph.Create("DDGI Ray Buffer", BufferDesc::CreateTyped(numProbes * ddgi.MaxNumRays, ResourceFormat::RGBA16_FLOAT));
				RGBuffer* pProbeStates = RGUtils::CreatePersistent(graph, "DDGI States Buffer", BufferDesc::CreateTyped(numProbes, ResourceFormat::R8_UINT), &ddgi.pProbeStates, true);
				RGBuffer* pProbeOffsets = RGUtils::CreatePersistent(graph, "DDGI Probe Offsets", BufferDesc::CreateTyped(numProbes, ResourceFormat::RGBA16_FLOAT), &ddgi.pProbeOffset, true);

				graph.AddPass("Raytrace", RGPassFlag::Compute)
					.Read(pProbeStates)
					.Write(pRayBuffer)
					.Bind([=](CommandContext& context, const RGResources& resources)
						{
							context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
							context.SetPipelineState(m_pDDGITraceRaysSO);

							Renderer::BindViewUniforms(context, *pView);
							context.BindRootCBV(BindingSlot::PerInstance, parameters);
							context.BindResources(BindingSlot::UAV, resources.GetUAV(pRayBuffer));

							ShaderBindingTable bindingTable(m_pDDGITraceRaysSO);
							bindingTable.BindRayGenShader("TraceRaysRGS");
							bindingTable.BindMissShader("MaterialMS", 0);
							bindingTable.BindMissShader("OcclusionMS", 1);
							bindingTable.BindHitGroup("MaterialHG", 0);

							context.DispatchRays(bindingTable, ddgi.NumRays, numProbes);
							context.InsertUAVBarrier(resources.Get(pRayBuffer));
						});

				graph.AddPass("Update Irradiance", RGPassFlag::Compute)
					.Read({ pIrradianceHistory, pRayBuffer, pProbeStates })
					.Write(pIrradianceTarget)
					.Bind([=](CommandContext& context, const RGResources& resources)
						{
							context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
							context.SetPipelineState(m_pDDGIUpdateIrradianceColorPSO);

							Renderer::BindViewUniforms(context, *pView);
							context.BindRootCBV(BindingSlot::PerInstance, parameters);
							context.BindResources(BindingSlot::UAV, resources.GetUAV(pIrradianceTarget));
							context.BindResources(BindingSlot::SRV, {
								resources.GetSRV(pRayBuffer),
								});

							context.Dispatch(numProbes);
							context.InsertUAVBarrier(resources.Get(pIrradianceTarget));
						});

				graph.AddPass("Update Depth", RGPassFlag::Compute)
					.Read({ pDepthHistory, pRayBuffer, pProbeStates })
					.Write(pDepthTarget)
					.Bind([=](CommandContext& context, const RGResources& resources)
						{
							context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
							context.SetPipelineState(m_pDDGIUpdateIrradianceDepthPSO);

							Renderer::BindViewUniforms(context, *pView);
							context.BindRootCBV(BindingSlot::PerInstance, parameters);
							context.BindResources(BindingSlot::UAV, {
								resources.GetUAV(pDepthTarget),
								});
							context.BindResources(BindingSlot::SRV, {
								resources.GetSRV(pRayBuffer),
								});

							context.Dispatch(numProbes);
							context.InsertUAVBarrier(resources.Get(pDepthTarget));
						});

				graph.AddPass("Update Probe States", RGPassFlag::Compute)
					.Read(pRayBuffer)
					.Write({ pProbeOffsets, pProbeStates })
					.Bind([=](CommandContext& context, const RGResources& resources)
						{
							context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
							context.SetPipelineState(m_pDDGIUpdateProbeStatesPSO);

							Renderer::BindViewUniforms(context, *pView);
							context.BindRootCBV(BindingSlot::PerInstance, parameters);
							context.BindResources(BindingSlot::UAV, {
								resources.GetUAV(pProbeStates),
								resources.GetUAV(pProbeOffsets),
								});
							context.BindResources(BindingSlot::SRV, {
								resources.GetSRV(pRayBuffer),
								});

							context.Dispatch(ComputeUtils::GetNumThreadGroups(numProbes, 32));
						});

				graph.AddPass("Bindless Transition", RGPassFlag::NeverCull | RGPassFlag::Raster)
					.Read({ pDepthTarget, pIrradianceTarget, pProbeStates, pProbeOffsets });
			});
	}
}

void DDGI::RenderVisualization(RGGraph& graph, const RenderView* pView, RGTexture* pColorTarget, RGTexture* pDepth)
{
	auto ddgi_view = pView->pWorld->Registry.view<const DDGIVolume>();
	int i = 0;
	ddgi_view.each([&](const DDGIVolume& volume)
		{
			graph.AddPass("DDGI Visualize", RGPassFlag::Raster)
				.DepthStencil(pDepth)
				.RenderTarget(pColorTarget)
				.Bind([=](CommandContext& context, const RGResources& resources)
					{
						context.SetGraphicsRootSignature(GraphicsCommon::pCommonRS);
						context.SetPipelineState(m_pDDGIVisualizePSO);
						context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

						Renderer::BindViewUniforms(context, *pView);

						struct
						{
							uint32 VolumeIndex;
						} parameters;
						parameters.VolumeIndex = i;

						context.BindRootCBV(BindingSlot::PerInstance, parameters);
						context.Draw(0, 2880, volume.NumProbes.x* volume.NumProbes.y* volume.NumProbes.z);
					});
			++i;
		});

}
