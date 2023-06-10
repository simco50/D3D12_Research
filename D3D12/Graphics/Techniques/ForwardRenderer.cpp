#include "stdafx.h"
#include "ForwardRenderer.h"
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
#include "Graphics/Light.h"
#include "Core/ConsoleVariables.h"

// Clustered
static constexpr int gLightClusterTexelSize = 64;
static constexpr int gLightClustersNumZ = 32;
static constexpr int gMaxLightsPerCluster = 32;

static constexpr int gVolumetricFroxelTexelSize = 8;
static constexpr int gVolumetricNumZSlices = 128;

// Tiled
static constexpr int MAX_LIGHT_DENSITY = 72000;
static constexpr int FORWARD_PLUS_BLOCK_SIZE = 16;

ForwardRenderer::ForwardRenderer(GraphicsDevice* pDevice)
	: m_pDevice(pDevice)
{
	m_pCommonRS = new RootSignature(pDevice);
	m_pCommonRS->AddRootConstants(0, 8);
	m_pCommonRS->AddRootCBV(100);
	m_pCommonRS->AddDescriptorTable(0, 8, D3D12_DESCRIPTOR_RANGE_TYPE_UAV);
	m_pCommonRS->AddDescriptorTable(0, 8, D3D12_DESCRIPTOR_RANGE_TYPE_SRV);
	m_pCommonRS->Finalize("Light Density Visualization");

	m_pForwardRS = new RootSignature(pDevice);
	m_pForwardRS->AddRootConstants(0, 6);
	m_pForwardRS->AddRootCBV(1);
	m_pForwardRS->AddRootCBV(100);
	m_pForwardRS->AddDescriptorTable(0, 8, D3D12_DESCRIPTOR_RANGE_TYPE_SRV);
	m_pForwardRS->Finalize("Forward");

	constexpr ResourceFormat formats[] = {
		ResourceFormat::RGBA16_FLOAT,
		ResourceFormat::RG16_FLOAT,
		ResourceFormat::R8_UNORM,
	};

	// Clustered
	{
		m_pClusteredCullPSO = pDevice->CreateComputePipeline(m_pCommonRS, "ClusteredLightCulling.hlsl", "LightCulling");

		//Opaque
		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pForwardRS);
		psoDesc.SetBlendMode(BlendMode::Replace, false);
		psoDesc.SetVertexShader("Diffuse.hlsl", "VSMain", { "CLUSTERED_FORWARD" });
		psoDesc.SetPixelShader("Diffuse.hlsl", "PSMain", { "CLUSTERED_FORWARD" });
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_EQUAL);
		psoDesc.SetDepthWrite(false);
		psoDesc.SetRenderTargetFormats(formats, GraphicsCommon::DepthStencilFormat, 1);
		psoDesc.SetName("Diffuse (Opaque)");
		m_pClusteredForwardPSO = pDevice->CreatePipeline(psoDesc);

		//Opaque Masked
		psoDesc.SetName("Diffuse Masked (Opaque)");
		psoDesc.SetCullMode(D3D12_CULL_MODE_NONE);
		m_pClusteredForwardMaskedPSO = pDevice->CreatePipeline(psoDesc);

		//Transparant
		psoDesc.SetName("Diffuse (Transparant)");
		psoDesc.SetBlendMode(BlendMode::Alpha, false);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
		m_pClusteredForwardAlphaBlendPSO = pDevice->CreatePipeline(psoDesc);

		m_pClusteredVisualizeLightsPSO = pDevice->CreateComputePipeline(m_pCommonRS, "VisualizeLightCount.hlsl", "DebugLightDensityCS", { "CLUSTERED_FORWARD" });
	}

	// Tiled
	{
		m_pTiledCullPSO = m_pDevice->CreateComputePipeline(m_pCommonRS, "LightCulling.hlsl", "CSMain");

		//Opaque
		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pForwardRS);
		psoDesc.SetVertexShader("Diffuse.hlsl", "VSMain", { "TILED_FORWARD" });
		psoDesc.SetPixelShader("Diffuse.hlsl", "PSMain", { "TILED_FORWARD" });
		psoDesc.SetRenderTargetFormats(formats, GraphicsCommon::DepthStencilFormat, 1);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_EQUAL);
		psoDesc.SetDepthWrite(false);
		psoDesc.SetName("Forward Pass - Opaque");
		m_pTiledForwardPSO = m_pDevice->CreatePipeline(psoDesc);

		//Alpha Mask
		psoDesc.SetCullMode(D3D12_CULL_MODE_NONE);
		psoDesc.SetName("Forward Pass - Opaque Masked");
		m_pTiledForwardMaskedPSO = m_pDevice->CreatePipeline(psoDesc);

		//Transparant
		psoDesc.SetBlendMode(BlendMode::Alpha, false);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
		psoDesc.SetName("Forward Pass - Transparent");
		m_pTiledForwardAlphaBlendPSO = m_pDevice->CreatePipeline(psoDesc);

		m_pTiledVisualizeLightsPSO = m_pDevice->CreateComputePipeline(m_pCommonRS, "VisualizeLightCount.hlsl", "DebugLightDensityCS", { "TILED_FORWARD" });
	}

}

ForwardRenderer::~ForwardRenderer()
{
}

void ForwardRenderer::ComputeClusteredLightCulling(RGGraph& graph, const SceneView* pView, LightCull3DData& cullData)
{
	RG_GRAPH_SCOPE("Light Culling", graph);

	cullData.ClusterCount.x = Math::DivideAndRoundUp(pView->GetDimensions().x, gLightClusterTexelSize);
	cullData.ClusterCount.y = Math::DivideAndRoundUp(pView->GetDimensions().y, gLightClusterTexelSize);
	cullData.ClusterCount.z = gLightClustersNumZ;
	float nearZ = pView->MainView.NearPlane;
	float farZ = pView->MainView.FarPlane;
	float n = Math::Min(nearZ, farZ);
	float f = Math::Max(nearZ, farZ);
	cullData.LightGridParams.x = (float)gLightClustersNumZ / log(f / n);
	cullData.LightGridParams.y = ((float)gLightClustersNumZ * log(n)) / log(f / n);
	cullData.ClusterSize = gLightClusterTexelSize;

	uint32 totalClusterCount = cullData.ClusterCount.x * cullData.ClusterCount.y * cullData.ClusterCount.z;

	cullData.pLightIndexGrid = graph.Create("Light Index Grid", BufferDesc::CreateTyped(gMaxLightsPerCluster * totalClusterCount, ResourceFormat::R16_UINT));
	// LightGrid: x : Offset | y : Count
	cullData.pLightGrid = graph.Create("Light Grid", BufferDesc::CreateTyped(totalClusterCount, ResourceFormat::R16_UINT));

	struct PrecomputedLightData
	{
		Vector3 ViewSpacePosition;
		float SpotCosAngle;
		Vector3 ViewSpaceDirection;
		float SpotSinAngle;
	};
	uint32 precomputedLightDataSize = sizeof(PrecomputedLightData) * pView->NumLights;

	RGBuffer* pPrecomputeData = graph.Create("Precompute Light Data", BufferDesc::CreateStructured(pView->NumLights, sizeof(PrecomputedLightData)));
	graph.AddPass("Precompute Light View Data", RGPassFlag::Copy)
		.Write(pPrecomputeData)
		.Bind([=](CommandContext& context)
			{
				DynamicAllocation allocation = context.AllocateTransientMemory(precomputedLightDataSize);
				PrecomputedLightData* pLightData = static_cast<PrecomputedLightData*>(allocation.pMappedMemory);

				const Matrix& viewMatrix = pView->MainView.View;
				for (const Light& light : pView->pWorld->Lights)
				{
					PrecomputedLightData& data = *pLightData++;
					data.ViewSpacePosition = Vector3::Transform(light.Position, viewMatrix);
					data.ViewSpaceDirection = Vector3::TransformNormal(Vector3::Transform(Vector3::Forward, light.Rotation), viewMatrix);
					data.SpotCosAngle = cos(light.UmbraAngleDegrees * Math::DegreesToRadians / 2.0f);
					data.SpotSinAngle = sin(light.UmbraAngleDegrees * Math::DegreesToRadians / 2.0f);
				}
				context.CopyBuffer(allocation.pBackingResource, pPrecomputeData->Get(), precomputedLightDataSize, allocation.Offset, 0);
			});

	graph.AddPass("Cull Lights", RGPassFlag::Compute)
		.Read(pPrecomputeData)
		.Write({ cullData.pLightGrid, cullData.pLightIndexGrid })
		.Bind([=](CommandContext& context)
			{
				context.SetPipelineState(m_pClusteredCullPSO);
				context.SetComputeRootSignature(m_pCommonRS);

				// Clear the light grid because we're accumulating the light count in the shader
				Buffer* pLightGrid = cullData.pLightGrid->Get();
				context.ClearUAVu(pLightGrid->GetUAV());

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
				context.BindResources(3, {
					pPrecomputeData->Get()->GetSRV()
					});

				context.Dispatch(
					ComputeUtils::GetNumThreadGroups(
						cullData.ClusterCount.x, 4,
						cullData.ClusterCount.y, 4,
						cullData.ClusterCount.z, 4)
				);
			});
}

void ForwardRenderer::RenderForwardClustered(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures, const LightCull3DData& lightCullData, RGTexture* pFogTexture, bool translucentOnly)
{
	RenderTargetLoadAction rtLoadOp = translucentOnly ? RenderTargetLoadAction::Load : RenderTargetLoadAction::DontCare;

	graph.AddPass("Base Pass", RGPassFlag::Raster)
		.Read({ sceneTextures.pAmbientOcclusion, sceneTextures.pPreviousColor, pFogTexture, sceneTextures.pDepth })
		.Read({ lightCullData.pLightGrid, lightCullData.pLightIndexGrid })
		.DepthStencil(sceneTextures.pDepth, rtLoadOp, false)
		.RenderTarget(sceneTextures.pColorTarget, rtLoadOp)
		.RenderTarget(sceneTextures.pNormals, rtLoadOp)
		.RenderTarget(sceneTextures.pRoughness, rtLoadOp)
		.Bind([=](CommandContext& context)
			{
				context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				context.SetGraphicsRootSignature(m_pForwardRS);

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

				if (!translucentOnly)
				{
					{
						GPU_PROFILE_SCOPE("Opaque", &context);
						context.SetPipelineState(m_pClusteredForwardPSO);
						Renderer::DrawScene(context, pView, Batch::Blending::Opaque);
					}
					{
						GPU_PROFILE_SCOPE("Opaque - Masked", &context);
						context.SetPipelineState(m_pClusteredForwardMaskedPSO);
						Renderer::DrawScene(context, pView, Batch::Blending::AlphaMask);
					}
				}
				{
					GPU_PROFILE_SCOPE("Transparant", &context);
					context.SetPipelineState(m_pClusteredForwardAlphaBlendPSO);
					Renderer::DrawScene(context, pView, Batch::Blending::AlphaBlend);
				}
			});
}


void ForwardRenderer::ComputeTiledLightCulling(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures, LightCull2DData& resources)
{
	int frustumCountX = Math::DivideAndRoundUp(pView->GetDimensions().x, FORWARD_PLUS_BLOCK_SIZE);
	int frustumCountY = Math::DivideAndRoundUp(pView->GetDimensions().y, FORWARD_PLUS_BLOCK_SIZE);
	resources.pLightGridOpaque = graph.Create("Light Grid - Opaque", TextureDesc::Create2D(frustumCountX, frustumCountY, ResourceFormat::RG16_UINT));
	resources.pLightGridTransparant = graph.Create("Light Grid - Transparant", TextureDesc::Create2D(frustumCountX, frustumCountY, ResourceFormat::RG16_UINT));

	resources.pLightIndexCounter = graph.Create("Light Index Counter", BufferDesc::CreateTyped(2, ResourceFormat::RG32_UINT));
	resources.pLightIndexListOpaque = graph.Create("Light List - Opaque", BufferDesc::CreateTyped(MAX_LIGHT_DENSITY, ResourceFormat::R16_UINT));
	resources.pLightIndexListTransparant = graph.Create("Light List - Transparant", BufferDesc::CreateTyped(MAX_LIGHT_DENSITY, ResourceFormat::R16_UINT));

	struct PrecomputedLightData
	{
		Vector3 ViewSpacePosition;
		float SpotCosAngle;
		Vector3 ViewSpaceDirection;
		float SpotSinAngle;
	};
	uint32 precomputedLightDataSize = sizeof(PrecomputedLightData) * pView->NumLights;

	RGBuffer* pPrecomputeData = graph.Create("Precompute Light Data", BufferDesc::CreateStructured(pView->NumLights, sizeof(PrecomputedLightData)));
	graph.AddPass("Precompute Light View Data", RGPassFlag::Copy)
		.Write(pPrecomputeData)
		.Bind([=](CommandContext& context)
			{
				DynamicAllocation allocation = context.AllocateTransientMemory(precomputedLightDataSize);
				PrecomputedLightData* pLightData = static_cast<PrecomputedLightData*>(allocation.pMappedMemory);

				const Matrix& viewMatrix = pView->MainView.View;
				for (const Light& light : pView->pWorld->Lights)
				{
					PrecomputedLightData& data = *pLightData++;
					data.ViewSpacePosition = Vector3::Transform(light.Position, viewMatrix);
					data.ViewSpaceDirection = Vector3::TransformNormal(Vector3::Transform(Vector3::Forward, light.Rotation), viewMatrix);
					data.SpotCosAngle = cos(light.UmbraAngleDegrees * Math::DegreesToRadians / 2.0f);
					data.SpotSinAngle = sin(light.UmbraAngleDegrees * Math::DegreesToRadians / 2.0f);
				}
				context.CopyBuffer(allocation.pBackingResource, pPrecomputeData->Get(), precomputedLightDataSize, allocation.Offset, 0);
			});

	graph.AddPass("2D Light Culling", RGPassFlag::Compute)
		.Read({ sceneTextures.pDepth, pPrecomputeData })
		.Write({ resources.pLightGridOpaque, resources.pLightIndexListOpaque })
		.Write({ resources.pLightGridTransparant, resources.pLightIndexListTransparant })
		.Write({ resources.pLightIndexCounter })
		.Bind([=](CommandContext& context)
			{
				Texture* pDepth = sceneTextures.pDepth->Get();

				context.ClearUAVu(resources.pLightIndexCounter->Get()->GetUAV());

				context.SetComputeRootSignature(m_pCommonRS);
				context.SetPipelineState(m_pTiledCullPSO);

				context.BindRootCBV(1, Renderer::GetViewUniforms(pView, pDepth));

				context.BindResources(2, {
					resources.pLightIndexCounter->Get()->GetUAV(),
					resources.pLightIndexListOpaque->Get()->GetUAV(),
					resources.pLightGridOpaque->Get()->GetUAV(),
					resources.pLightIndexListTransparant->Get()->GetUAV(),
					resources.pLightGridTransparant->Get()->GetUAV(),
					});
				context.BindResources(3, {
					pDepth->GetSRV(),
					pPrecomputeData->Get()->GetSRV(),
					});

				context.Dispatch(ComputeUtils::GetNumThreadGroups(
					pDepth->GetWidth(), FORWARD_PLUS_BLOCK_SIZE,
					pDepth->GetHeight(), FORWARD_PLUS_BLOCK_SIZE
				));
			});
}

void ForwardRenderer::RenderForwardTiled(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures, const LightCull2DData& lightCullData, RGTexture* pFogTexture)
{
	graph.AddPass("Forward Pass", RGPassFlag::Raster)
		.Read({ sceneTextures.pAmbientOcclusion, sceneTextures.pPreviousColor, pFogTexture })
		.Read({ lightCullData.pLightGridOpaque, lightCullData.pLightGridTransparant, lightCullData.pLightIndexListOpaque, lightCullData.pLightIndexListTransparant })
		.DepthStencil(sceneTextures.pDepth, RenderTargetLoadAction::Load, false)
		.RenderTarget(sceneTextures.pColorTarget, RenderTargetLoadAction::DontCare)
		.RenderTarget(sceneTextures.pNormals, RenderTargetLoadAction::DontCare)
		.RenderTarget(sceneTextures.pRoughness, RenderTargetLoadAction::DontCare)
		.Bind([=](CommandContext& context)
			{
				context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				context.SetGraphicsRootSignature(m_pForwardRS);

				context.BindRootCBV(1, Renderer::GetViewUniforms(pView, sceneTextures.pColorTarget->Get()));

				{
					context.BindResources(3, {
						sceneTextures.pAmbientOcclusion->Get()->GetSRV(),
						sceneTextures.pDepth->Get()->GetSRV(),
						sceneTextures.pPreviousColor->Get()->GetSRV(),
						pFogTexture->Get()->GetSRV(),
						lightCullData.pLightGridOpaque->Get()->GetSRV(),
						lightCullData.pLightIndexListOpaque->Get()->GetSRV(),
						});

					{
						GPU_PROFILE_SCOPE("Opaque", &context);
						context.SetPipelineState(m_pTiledForwardPSO);
						Renderer::DrawScene(context, pView, Batch::Blending::Opaque);
					}

					{
						GPU_PROFILE_SCOPE("Opaque Masked", &context);
						context.SetPipelineState(m_pTiledForwardMaskedPSO);
						Renderer::DrawScene(context, pView, Batch::Blending::AlphaMask);
					}
				}

				{
					context.BindResources(3, {
						sceneTextures.pAmbientOcclusion->Get()->GetSRV(),
						sceneTextures.pDepth->Get()->GetSRV(),
						sceneTextures.pPreviousColor->Get()->GetSRV(),
						pFogTexture->Get()->GetSRV(),
						lightCullData.pLightGridTransparant->Get()->GetSRV(),
						lightCullData.pLightIndexListTransparant->Get()->GetSRV(),
						});

					{
						GPU_PROFILE_SCOPE("Transparant", &context);
						context.SetPipelineState(m_pTiledForwardAlphaBlendPSO);
						Renderer::DrawScene(context, pView, Batch::Blending::AlphaBlend);
					}
				}
			});
}

void ForwardRenderer::VisualizeLightDensity(RGGraph& graph, GraphicsDevice* pDevice, const SceneView* pView, SceneTextures& sceneTextures, const LightCull2DData& lightCullData)
{
	RGTexture* pVisualizationTarget = graph.Create("Scene Color", sceneTextures.pColorTarget->GetDesc());
	RGTexture* pLightGridOpaque = lightCullData.pLightGridOpaque;

	graph.AddPass("Visualize Light Density", RGPassFlag::Compute)
		.Read({ sceneTextures.pDepth, sceneTextures.pColorTarget, pLightGridOpaque })
		.Write(pVisualizationTarget)
		.Bind([=](CommandContext& context)
			{
				Texture* pTarget = pVisualizationTarget->Get();

				context.SetComputeRootSignature(m_pCommonRS);
				context.SetPipelineState(m_pTiledVisualizeLightsPSO);

				context.BindRootCBV(1, Renderer::GetViewUniforms(pView, pTarget));
				context.BindResources(2, pTarget->GetUAV());
				context.BindResources(3, {
					sceneTextures.pColorTarget->Get()->GetSRV(),
					sceneTextures.pDepth->Get()->GetSRV(),
					pLightGridOpaque->Get()->GetSRV(),
					});

				context.Dispatch(ComputeUtils::GetNumThreadGroups(
					pTarget->GetWidth(), 16,
					pTarget->GetHeight(), 16));
			});

	sceneTextures.pColorTarget = pVisualizationTarget;
}

void ForwardRenderer::VisualizeLightDensity(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures, const LightCull3DData& lightCullData)
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

				context.SetComputeRootSignature(m_pCommonRS);
				context.SetPipelineState(m_pClusteredVisualizeLightsPSO);

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
