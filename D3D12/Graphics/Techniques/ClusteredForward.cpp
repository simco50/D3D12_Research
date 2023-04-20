#include "stdafx.h"
#include "ClusteredForward.h"
#include "Graphics/RHI/PipelineState.h"
#include "Graphics/RHI/RootSignature.h"
#include "Graphics/RHI/Buffer.h"
#include "Graphics/RHI/Graphics.h"
#include "Graphics/RHI/CommandContext.h"
#include "Graphics/RHI/Texture.h"
#include "Graphics/RHI/ResourceViews.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/Profiler.h"
#include "Graphics/SceneView.h"
#include "Core/ConsoleVariables.h"

static constexpr int gLightClusterTexelSize = 64;
static constexpr int gLightClustersNumZ = 32;
static constexpr int gMaxLightsPerCluster = 32;

static constexpr int gVolumetricFroxelTexelSize = 8;
static constexpr int gVolumetricNumZSlices = 128;

ClusteredForward::ClusteredForward(GraphicsDevice* pDevice)
	: m_pDevice(pDevice)
{
	CommandContext* pContext = pDevice->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
	m_pHeatMapTexture = GraphicsCommon::CreateTextureFromFile(*pContext, "Resources/Textures/Heatmap.png", true, "Color Heatmap");
	pContext->Execute();

	m_pCommonRS = new RootSignature(pDevice);
	m_pCommonRS->AddRootCBV(0);
	m_pCommonRS->AddRootCBV(100);
	m_pCommonRS->AddDescriptorTable(0, 8, D3D12_DESCRIPTOR_RANGE_TYPE_UAV);
	m_pCommonRS->AddDescriptorTable(0, 8, D3D12_DESCRIPTOR_RANGE_TYPE_SRV);
	m_pCommonRS->Finalize("Light Density Visualization");

	//Light Culling
	m_pLightCullingPSO = pDevice->CreateComputePipeline(m_pCommonRS, "ClusteredLightCulling.hlsl", "LightCulling");

	//Diffuse
	{
		m_pDiffuseRS = new RootSignature(pDevice);
		m_pDiffuseRS->AddRootConstants(0, 3);
		m_pDiffuseRS->AddRootCBV(1);
		m_pDiffuseRS->AddRootCBV(100);
		m_pDiffuseRS->AddDescriptorTable(0, 8, D3D12_DESCRIPTOR_RANGE_TYPE_SRV);
		m_pDiffuseRS->Finalize("Diffuse");

		constexpr ResourceFormat formats[] = {
			ResourceFormat::RGBA16_FLOAT,
			ResourceFormat::RG16_FLOAT,
			ResourceFormat::R8_UNORM,
		};

		{
			//Opaque
			PipelineStateInitializer psoDesc;
			psoDesc.SetRootSignature(m_pDiffuseRS);
			psoDesc.SetBlendMode(BlendMode::Replace, false);
			psoDesc.SetVertexShader("Diffuse.hlsl", "VSMain", { "CLUSTERED_FORWARD" });
			psoDesc.SetPixelShader("Diffuse.hlsl", "PSMain", { "CLUSTERED_FORWARD" });
			psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_EQUAL);
			psoDesc.SetDepthWrite(false);
			psoDesc.SetRenderTargetFormats(formats, GraphicsCommon::DepthStencilFormat, 1);
			psoDesc.SetName("Diffuse (Opaque)");
			m_pDiffusePSO = pDevice->CreatePipeline(psoDesc);

			//Opaque Masked
			psoDesc.SetName("Diffuse Masked (Opaque)");
			psoDesc.SetCullMode(D3D12_CULL_MODE_NONE);
			m_pDiffuseMaskedPSO = pDevice->CreatePipeline(psoDesc);

			//Transparant
			psoDesc.SetName("Diffuse (Transparant)");
			psoDesc.SetBlendMode(BlendMode::Alpha, false);
			psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
			m_pDiffuseTransparancyPSO = pDevice->CreatePipeline(psoDesc);
		}

		if (pDevice->GetCapabilities().SupportsMeshShading())
		{
			//Opaque
			PipelineStateInitializer psoDesc;
			psoDesc.SetRootSignature(m_pDiffuseRS);
			psoDesc.SetBlendMode(BlendMode::Replace, false);
			psoDesc.SetMeshShader("Diffuse.hlsl", "MSMain", { "CLUSTERED_FORWARD" });
			psoDesc.SetAmplificationShader("Diffuse.hlsl", "ASMain", { "CLUSTERED_FORWARD" });
			psoDesc.SetPixelShader("Diffuse.hlsl", "PSMain", { "CLUSTERED_FORWARD" });
			psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_EQUAL);
			psoDesc.SetDepthWrite(false);
			psoDesc.SetRenderTargetFormats(formats, GraphicsCommon::DepthStencilFormat, 1);
			psoDesc.SetName("Diffuse (Opaque)");
			m_pMeshShaderDiffusePSO = pDevice->CreatePipeline(psoDesc);

			//Opaque Masked
			psoDesc.SetName("Diffuse Masked (Opaque)");
			psoDesc.SetCullMode(D3D12_CULL_MODE_NONE);
			m_pMeshShaderDiffuseMaskedPSO = pDevice->CreatePipeline(psoDesc);

			//Transparant
			psoDesc.SetName("Diffuse (Transparant)");
			psoDesc.SetBlendMode(BlendMode::Alpha, false);
			psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
			m_pMeshShaderDiffuseTransparancyPSO = pDevice->CreatePipeline(psoDesc);
		}
	}

	//Cluster debug rendering
	{
		PipelineStateInitializer psoDesc;
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
		psoDesc.SetDepthWrite(false);
		psoDesc.SetPixelShader("VisualizeLightClusters.hlsl", "PSMain");
		psoDesc.SetRenderTargetFormats(ResourceFormat::RGBA16_FLOAT, GraphicsCommon::DepthStencilFormat, 1);
		psoDesc.SetBlendMode(BlendMode::Additive, false);
		psoDesc.SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT);
		psoDesc.SetRootSignature(m_pCommonRS);
		psoDesc.SetVertexShader("VisualizeLightClusters.hlsl", "VSMain");
		psoDesc.SetGeometryShader("VisualizeLightClusters.hlsl", "GSMain");
		psoDesc.SetName("Visualize Light Clusters");
		m_pVisualizeLightClustersPSO = pDevice->CreatePipeline(psoDesc);
	}

	m_pVisualizeLightsPSO = pDevice->CreateComputePipeline(m_pCommonRS, "VisualizeLightCount.hlsl", "DebugLightDensityCS", { "CLUSTERED_FORWARD" });

	// Volumetric fog
	m_pInjectVolumeLightPSO = pDevice->CreateComputePipeline(m_pCommonRS, "VolumetricFog.hlsl", "InjectFogLightingCS");
	m_pAccumulateVolumeLightPSO = pDevice->CreateComputePipeline(m_pCommonRS, "VolumetricFog.hlsl", "AccumulateFogCS");
}

ClusteredForward::~ClusteredForward()
{
}

void ClusteredForward::ComputeLightCulling(RGGraph& graph, const SceneView* pView, LightCull3DData& cullData)
{
	RG_GRAPH_SCOPE("Light Culling", graph);

	cullData.ClusterCount.x = Math::DivideAndRoundUp(pView->GetDimensions().x, gLightClusterTexelSize);
	cullData.ClusterCount.y = Math::DivideAndRoundUp(pView->GetDimensions().y, gLightClusterTexelSize);
	cullData.ClusterCount.z = gLightClustersNumZ;
	float nearZ = pView->View.NearPlane;
	float farZ = pView->View.FarPlane;
	float n = Math::Min(nearZ, farZ);
	float f = Math::Max(nearZ, farZ);
	cullData.LightGridParams.x = (float)gLightClustersNumZ / log(f / n);
	cullData.LightGridParams.y = ((float)gLightClustersNumZ * log(n)) / log(f / n);
	cullData.ClusterSize = gLightClusterTexelSize;

	uint32 totalClusterCount = cullData.ClusterCount.x * cullData.ClusterCount.y * cullData.ClusterCount.z;

	cullData.pLightIndexGrid = graph.Create("Light Index Grid", BufferDesc::CreateStructured(gMaxLightsPerCluster * totalClusterCount, sizeof(uint32)));
	// LightGrid: x : Offset | y : Count
	cullData.pLightGrid = graph.Create("Light Grid", BufferDesc::CreateStructured(2 * totalClusterCount, sizeof(uint32)));

	graph.AddPass("Cull Lights", RGPassFlag::Compute)
		.Read(cullData.pAABBs)
		.Write({ cullData.pLightGrid, cullData.pLightIndexGrid })
		.Bind([=](CommandContext& context)
			{
				context.SetPipelineState(m_pLightCullingPSO);
				context.SetComputeRootSignature(m_pCommonRS);

				// Clear the light grid because we're accumulating the light count in the shader
				Buffer* pLightGrid = cullData.pLightGrid->Get();
				//#todo: adhoc UAV creation
				context.ClearUAVu(m_pDevice->CreateUAV(pLightGrid, BufferUAVDesc::CreateRaw()));

				struct
				{
					Vector4i ClusterDimensions;
					Vector2i ClusterSize;

				} constantBuffer;

				constantBuffer.ClusterSize = Vector2i(gLightClusterTexelSize, gLightClusterTexelSize);
				constantBuffer.ClusterDimensions = Vector4i(cullData.ClusterCount.x, cullData.ClusterCount.y, cullData.ClusterCount.z, 0);

				context.BindRootCBV(0, constantBuffer);

				context.BindRootCBV(1, Renderer::GetViewUniforms(pView));
				context.BindResources(2, {
					cullData.pLightIndexGrid->Get()->GetUAV(),
					cullData.pLightGrid->Get()->GetUAV(),
					});

				context.Dispatch(
					ComputeUtils::GetNumThreadGroups(
						cullData.ClusterCount.x, 4,
						cullData.ClusterCount.y, 4,
						cullData.ClusterCount.z, 4)
				);
			});
}

void ClusteredForward::VisualizeClusters(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures, LightCull3DData& cullData)
{
	uint32 totalClusterCount = cullData.ClusterCount.x * cullData.ClusterCount.y * cullData.ClusterCount.z;
	RGBuffer* pDebugLightGrid = RGUtils::CreatePersistent(graph, "Debug Light Grid", BufferDesc::CreateStructured(2 * totalClusterCount, sizeof(uint32)), &cullData.pDebugLightGrid, true);

	if (cullData.DirtyDebugData)
	{
		RGUtils::AddCopyPass(graph, cullData.pLightGrid, pDebugLightGrid);
		cullData.DebugClustersViewMatrix = pView->View.ViewInverse;
		cullData.DirtyDebugData = false;
	}

	graph.AddPass("Visualize Clusters", RGPassFlag::Raster)
		.Read({ pDebugLightGrid, cullData.pAABBs })
		.RenderTarget(sceneTextures.pColorTarget, RenderTargetLoadAction::Load)
		.DepthStencil(sceneTextures.pDepth, RenderTargetLoadAction::Load, false)
		.Bind([=](CommandContext& context)
			{
				context.SetPipelineState(m_pVisualizeLightClustersPSO);
				context.SetGraphicsRootSignature(m_pCommonRS);
				context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);

				ShaderInterop::ViewUniforms viewData = Renderer::GetViewUniforms(pView, sceneTextures.pColorTarget->Get());
				viewData.Projection = cullData.DebugClustersViewMatrix * pView->View.ViewProjection;
				context.BindRootCBV(1, viewData);
				context.BindResources(3, {
					cullData.pAABBs->Get()->GetSRV(),
					pDebugLightGrid->Get()->GetSRV(),
					m_pHeatMapTexture->GetSRV(),
					});
				context.Draw(0, cullData.ClusterCount.x * cullData.ClusterCount.y * cullData.ClusterCount.z);
			});
}

RGTexture* ClusteredForward::RenderVolumetricFog(RGGraph& graph, const SceneView* pView, const LightCull3DData& lightCullData, VolumetricFogData& fogData)
{
	RG_GRAPH_SCOPE("Volumetric Lighting", graph);

	TextureDesc volumeDesc = TextureDesc::Create3D(
		Math::DivideAndRoundUp((uint32)pView->GetDimensions().x, gVolumetricFroxelTexelSize),
		Math::DivideAndRoundUp((uint32)pView->GetDimensions().y, gVolumetricFroxelTexelSize),
		gVolumetricNumZSlices,
		ResourceFormat::RGBA16_FLOAT,
		TextureFlag::ShaderResource | TextureFlag::UnorderedAccess);

	RGTexture* pSourceVolume = RGUtils::CreatePersistent(graph, "Fog History", volumeDesc, &fogData.pFogHistory, false);
	RGTexture* pTargetVolume = graph.Create("Fog Target", volumeDesc);
	RGTexture* pFinalVolumeFog = graph.Create("Volumetric Fog", volumeDesc);
	graph.Export(pTargetVolume, &fogData.pFogHistory);

	struct
	{
		Vector3i ClusterDimensions;
		float Jitter;
		Vector3 InvClusterDimensions;
		float LightClusterSizeFactor;
		Vector2 LightGridParams;
		Vector2i LightClusterDimensions;
	} constantBuffer;

	constantBuffer.ClusterDimensions = Vector3i(volumeDesc.Width, volumeDesc.Height, volumeDesc.DepthOrArraySize);
	constantBuffer.InvClusterDimensions = Vector3(1.0f / volumeDesc.Width, 1.0f / volumeDesc.Height, 1.0f / volumeDesc.DepthOrArraySize);
	constexpr Math::HaltonSequence<32, 2> halton;
	constantBuffer.Jitter = halton[pView->FrameIndex & 31];
	constantBuffer.LightClusterSizeFactor = (float)gVolumetricFroxelTexelSize / gLightClusterTexelSize;
	constantBuffer.LightGridParams = lightCullData.LightGridParams;
	constantBuffer.LightClusterDimensions = Vector2i(lightCullData.ClusterCount.x, lightCullData.ClusterCount.y);

	graph.AddPass("Inject Volume Lights", RGPassFlag::Compute)
		.Read({ pSourceVolume, lightCullData.pLightGrid, lightCullData.pLightIndexGrid })
		.Write(pTargetVolume)
		.Bind([=](CommandContext& context)
			{
				Texture* pTarget = pTargetVolume->Get();

				context.SetComputeRootSignature(m_pCommonRS);
				context.SetPipelineState(m_pInjectVolumeLightPSO);

				context.BindRootCBV(0, constantBuffer);
				context.BindRootCBV(1, Renderer::GetViewUniforms(pView));
				context.BindResources(2, pTarget->GetUAV());
				context.BindResources(3, {
					lightCullData.pLightGrid->Get()->GetSRV(),
					lightCullData.pLightIndexGrid->Get()->GetSRV(),
					pSourceVolume->Get()->GetSRV(),
					});

				context.Dispatch(
					ComputeUtils::GetNumThreadGroups(
						pTarget->GetWidth(), 8,
						pTarget->GetHeight(), 8,
						pTarget->GetDepth(), 4)
				);
			});

	graph.AddPass("Accumulate Volume Fog", RGPassFlag::Compute)
		.Read({ pTargetVolume, lightCullData.pLightGrid, lightCullData.pLightIndexGrid })
		.Write(pFinalVolumeFog)
		.Bind([=](CommandContext& context)
			{
				Texture* pFinalFog = pFinalVolumeFog->Get();

				context.SetComputeRootSignature(m_pCommonRS);
				context.SetPipelineState(m_pAccumulateVolumeLightPSO);

				context.BindRootCBV(0, constantBuffer);
				context.BindRootCBV(1, Renderer::GetViewUniforms(pView));
				context.BindResources(2, pFinalFog->GetUAV());
				context.BindResources(3, {
					lightCullData.pLightGrid->Get()->GetSRV(),
					lightCullData.pLightIndexGrid->Get()->GetSRV(),
					pTargetVolume->Get()->GetSRV(),
					});

				context.Dispatch(
					ComputeUtils::GetNumThreadGroups(
						pFinalFog->GetWidth(), 8,
						pFinalFog->GetHeight(), 8));
			});
	return pFinalVolumeFog;
}

void ClusteredForward::RenderBasePass(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures, const LightCull3DData& lightCullData, RGTexture* pFogTexture)
{
	static constexpr bool useMeshShader = false;

	graph.AddPass("Base Pass", RGPassFlag::Raster)
		.Read({ sceneTextures.pAmbientOcclusion, sceneTextures.pPreviousColor, pFogTexture, sceneTextures.pDepth })
		.Read({ lightCullData.pLightGrid, lightCullData.pLightIndexGrid })
		.DepthStencil(sceneTextures.pDepth, RenderTargetLoadAction::Load, false)
		.RenderTarget(sceneTextures.pColorTarget, RenderTargetLoadAction::DontCare)
		.RenderTarget(sceneTextures.pNormals, RenderTargetLoadAction::DontCare)
		.RenderTarget(sceneTextures.pRoughness, RenderTargetLoadAction::DontCare)
		.Bind([=](CommandContext& context)
			{
				context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				context.SetGraphicsRootSignature(m_pDiffuseRS);

				struct
				{
					Vector4i ClusterDimensions;
					Vector2i ClusterSize;
					Vector2 LightGridParams;
				} frameData;

				frameData.ClusterDimensions = Vector4i(lightCullData.ClusterCount.x, lightCullData.ClusterCount.y, lightCullData.ClusterCount.z, 0);
				frameData.ClusterSize = Vector2i(gLightClusterTexelSize, gLightClusterTexelSize);
				frameData.LightGridParams = lightCullData.LightGridParams;

				context.BindRootCBV(1, frameData);
				context.BindRootCBV(2, Renderer::GetViewUniforms(pView, sceneTextures.pColorTarget->Get()));

				context.BindResources(3, {
					sceneTextures.pAmbientOcclusion->Get()->GetSRV(),
					sceneTextures.pDepth->Get()->GetSRV(),
					sceneTextures.pPreviousColor->Get()->GetSRV(),
					pFogTexture->Get()->GetSRV(),
					lightCullData.pLightGrid->Get()->GetSRV(),
					lightCullData.pLightIndexGrid->Get()->GetSRV(),
					});

				{
					GPU_PROFILE_SCOPE("Opaque", &context);
					context.SetPipelineState(useMeshShader ? m_pMeshShaderDiffusePSO : m_pDiffusePSO);
					Renderer::DrawScene(context, pView, Batch::Blending::Opaque);
				}
				{
					GPU_PROFILE_SCOPE("Opaque - Masked", &context);
					context.SetPipelineState(useMeshShader ? m_pMeshShaderDiffuseMaskedPSO : m_pDiffuseMaskedPSO);
					Renderer::DrawScene(context, pView, Batch::Blending::AlphaMask);
				}
				{
					GPU_PROFILE_SCOPE("Transparant", &context);
					context.SetPipelineState(useMeshShader ? m_pMeshShaderDiffuseTransparancyPSO : m_pDiffuseTransparancyPSO);
					Renderer::DrawScene(context, pView, Batch::Blending::AlphaBlend);
				}
			});
}

void ClusteredForward::VisualizeLightDensity(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures, const LightCull3DData& lightCullData)
{
	RGTexture* pVisualizationTarget = graph.Create("Scene Color", sceneTextures.pColorTarget->GetDesc());

	RGBuffer* pLightGrid = lightCullData.pLightGrid;
	Vector2 lightGridParams = lightCullData.LightGridParams;
	Vector3i clusterCount = lightCullData.ClusterCount;

	graph.AddPass("Visualize Light Density", RGPassFlag::Compute)
		.Read({ sceneTextures.pDepth, sceneTextures.pColorTarget, pLightGrid })
		.Write(pVisualizationTarget)
		.Bind([=](CommandContext& context)
			{
				Texture* pTarget = pVisualizationTarget->Get();

				struct
				{
					Vector2i ClusterDimensions;
					Vector2i ClusterSize;
					Vector2 LightGridParams;
				} constantBuffer;

				constantBuffer.ClusterDimensions = Vector2i(clusterCount.x, clusterCount.y);
				constantBuffer.ClusterSize = Vector2i(gLightClusterTexelSize, gLightClusterTexelSize);
				constantBuffer.LightGridParams = lightGridParams;

				context.SetPipelineState(m_pVisualizeLightsPSO);
				context.SetComputeRootSignature(m_pCommonRS);
				context.BindRootCBV(0, constantBuffer);
				context.BindRootCBV(1, Renderer::GetViewUniforms(pView, pTarget));
				context.BindResources(2, pTarget->GetUAV());
				context.BindResources(3, {
					sceneTextures.pColorTarget->Get()->GetSRV(),
					sceneTextures.pDepth->Get()->GetSRV(),
					pLightGrid->Get()->GetSRV(),
					});

				context.Dispatch(ComputeUtils::GetNumThreadGroups(
					pTarget->GetWidth(), 16,
					pTarget->GetHeight(), 16));
			});

	sceneTextures.pColorTarget = pVisualizationTarget;
}
