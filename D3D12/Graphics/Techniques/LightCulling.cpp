#include "stdafx.h"
#include "LightCulling.h"
#include "Graphics/RHI/PipelineState.h"
#include "Graphics/RHI/RootSignature.h"
#include "Graphics/RHI/Buffer.h"
#include "Graphics/RHI/Device.h"
#include "Graphics/RHI/CommandContext.h"
#include "Graphics/RHI/Texture.h"
#include "Graphics/RHI/ResourceViews.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Core/Profiler.h"
#include "Graphics/SceneView.h"
#include "Graphics/Light.h"
#include "Core/ConsoleVariables.h"

// Clustered
static constexpr int gLightClusterTexelSize = 64;
static constexpr int gLightClustersNumZ = 32;
static constexpr int gMaxLightsPerCluster = 256;
static_assert(gMaxLightsPerCluster % 32 == 0);

// Tiled
static constexpr int gTiledLightingTileSize = 8;
static constexpr int gMaxLightsPerTile = 256;
static_assert(gMaxLightsPerTile % 32 == 0);

LightCulling::LightCulling(GraphicsDevice* pDevice)
	: m_pDevice(pDevice)
{
	// Clustered
	m_pClusteredCullPSO = pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "ClusteredLightCulling.hlsl", "LightCulling");
	m_pClusteredVisualizeLightsPSO = pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "VisualizeLightCount.hlsl", "DebugLightDensityCS", { "CLUSTERED_FORWARD" });

	// Tiled
	m_pTiledCullPSO = m_pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "LightCulling.hlsl", "CSMain");
	m_pTiledVisualizeLightsPSO = m_pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "VisualizeLightCount.hlsl", "DebugLightDensityCS", { "TILED_FORWARD" });
}

LightCulling::~LightCulling()
{
}

void LightCulling::ComputeClusteredLightCulling(RGGraph& graph, const SceneView* pView, LightCull3DData& cullData)
{
	RG_GRAPH_SCOPE("Clustered Light Culling", graph);

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

	cullData.pLightGrid = graph.Create("Light Index Grid", BufferDesc::CreateTyped(gMaxLightsPerCluster / 32 * totalClusterCount, ResourceFormat::R32_UINT));

	struct PrecomputedLightData
	{
		Vector3 ViewSpacePosition;
		float SpotCosAngle;
		Vector3 ViewSpaceDirection;
		float SpotSinAngle;
		float Range;
		uint32 IsSpot : 1;
		uint32 IsPoint : 1;
		uint32 IsDirectional : 1;
	};
	uint32 precomputedLightDataSize = sizeof(PrecomputedLightData) * pView->LightBuffer.Count;

	RGBuffer* pPrecomputeData = graph.Create("Precompute Light Data", BufferDesc::CreateStructured(pView->LightBuffer.Count, sizeof(PrecomputedLightData)));
	graph.AddPass("Precompute Light View Data", RGPassFlag::Copy)
		.Write(pPrecomputeData)
		.Bind([=](CommandContext& context)
			{
				ScratchAllocation allocation = context.AllocateScratch(precomputedLightDataSize);
				PrecomputedLightData* pLightData = static_cast<PrecomputedLightData*>(allocation.pMappedMemory);

				const Matrix& viewMatrix = pView->MainView.View;
				auto light_view = pView->pWorld->Registry.view<const Transform, const Light>();
				light_view.each([&](const Transform& transform, const Light& light)
					{
						PrecomputedLightData& data = *pLightData++;
						data.ViewSpacePosition = Vector3::Transform(transform.Position, viewMatrix);
						data.ViewSpaceDirection = Vector3::TransformNormal(Vector3::Transform(Vector3::Forward, transform.Rotation), viewMatrix);
						data.SpotCosAngle = cos(light.UmbraAngleDegrees * Math::DegreesToRadians / 2.0f);
						data.SpotSinAngle = sin(light.UmbraAngleDegrees * Math::DegreesToRadians / 2.0f);
						data.Range = light.Range;
						data.IsSpot = light.Type == LightType::Spot;
						data.IsPoint = light.Type == LightType::Point;
						data.IsDirectional = light.Type == LightType::Directional;
					});
				context.CopyBuffer(allocation.pBackingResource, pPrecomputeData->Get(), precomputedLightDataSize, allocation.Offset, 0);
			});

	graph.AddPass("Cull Lights", RGPassFlag::Compute)
		.Read(pPrecomputeData)
		.Write({ cullData.pLightGrid })
		.Bind([=](CommandContext& context)
			{
				context.SetPipelineState(m_pClusteredCullPSO);
				context.SetComputeRootSignature(GraphicsCommon::pCommonRS);

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

void LightCulling::ComputeTiledLightCulling(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures, LightCull2DData& resources)
{
	RG_GRAPH_SCOPE("Tiled Light Culling", graph);

	uint32 tilesX = Math::DivideAndRoundUp((uint32)pView->MainView.Viewport.GetWidth(), gTiledLightingTileSize);
	uint32 tilesY = Math::DivideAndRoundUp((uint32)pView->MainView.Viewport.GetHeight(), gTiledLightingTileSize);
	uint32 lightListElements = tilesX * tilesY * (gMaxLightsPerTile / 32);

	resources.pLightListOpaque = graph.Create("Light List - Opaque", BufferDesc::CreateTyped(lightListElements, ResourceFormat::R32_UINT));
	resources.pLightListTransparent = graph.Create("Light List - Transparant", BufferDesc::CreateTyped(lightListElements, ResourceFormat::R32_UINT));

	struct PrecomputedLightData
	{
		Vector3 SphereViewPosition;
		float SphereRadius;
	};

	RGBuffer* pPrecomputeData = graph.Create("Precompute Light Data", BufferDesc::CreateStructured(pView->LightBuffer.Count, sizeof(PrecomputedLightData)));
	graph.AddPass("Precompute Light View Data", RGPassFlag::Copy)
		.Write(pPrecomputeData)
		.Bind([=](CommandContext& context)
			{
				uint32 precomputedLightDataSize = sizeof(PrecomputedLightData) * pView->LightBuffer.Count;
				ScratchAllocation allocation = context.AllocateScratch(precomputedLightDataSize);
				PrecomputedLightData* pLightData = static_cast<PrecomputedLightData*>(allocation.pMappedMemory);

				const Matrix& viewMatrix = pView->MainView.View;
				auto light_view = pView->pWorld->Registry.view<const Transform, const Light>();
				light_view.each([&](const Transform& transform, const Light& light)
					{
						PrecomputedLightData& data = *pLightData++;
						if (light.Type == LightType::Directional)
						{
							data.SphereRadius = FLT_MAX;
							data.SphereViewPosition = Vector3::Zero;
						}
						else if (light.Type == LightType::Point)
						{
							data.SphereRadius = light.Range;
							data.SphereViewPosition = Vector3::Transform(transform.Position, viewMatrix);
						}
						else if (light.Type == LightType::Spot)
						{
							float cosAngle = cos(light.UmbraAngleDegrees * Math::DegreesToRadians / 2.0f);

							data.SphereRadius = light.Range * 0.5f / powf(cosAngle, 2);
							data.SphereViewPosition = Vector3::Transform(transform.Position, viewMatrix) + Vector3::TransformNormal(Vector3::Transform(Vector3::Forward, transform.Rotation), viewMatrix) * light.Range;
						}
					});
				context.CopyBuffer(allocation.pBackingResource, pPrecomputeData->Get(), precomputedLightDataSize, allocation.Offset, 0);
			});

	graph.AddPass("2D Light Culling", RGPassFlag::Compute)
		.Read({ sceneTextures.pDepth, pPrecomputeData })
		.Write({ resources.pLightListOpaque, resources.pLightListTransparent })
		.Bind([=](CommandContext& context)
			{
				Texture* pDepth = sceneTextures.pDepth->Get();

				context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
				context.SetPipelineState(m_pTiledCullPSO);

				context.BindRootCBV(1, Renderer::GetViewUniforms(pView, pDepth));

				context.BindResources(2, {
					resources.pLightListOpaque->Get()->GetUAV(),
					resources.pLightListTransparent->Get()->GetUAV(),
					});
				context.BindResources(3, {
					pDepth->GetSRV(),
					pPrecomputeData->Get()->GetSRV(),
					});

				context.Dispatch(ComputeUtils::GetNumThreadGroups(
					pDepth->GetWidth(), gTiledLightingTileSize,
					pDepth->GetHeight(), gTiledLightingTileSize
				));
			});
}

RGTexture* LightCulling::VisualizeLightDensity(RGGraph& graph, const SceneView* pView, RGTexture* pSceneDepth, const LightCull2DData& lightCullData)
{
	RGTexture* pVisualizationTarget = graph.Create("Light Density Visualization", TextureDesc::Create2D(pSceneDepth->GetDesc().Width, pSceneDepth->GetDesc().Height, ResourceFormat::RGBA8_UNORM, 1));

	graph.AddPass("Visualize Light Density", RGPassFlag::Compute)
		.Read({ pSceneDepth, lightCullData.pLightListOpaque })
		.Write(pVisualizationTarget)
		.Bind([=](CommandContext& context)
			{
				Texture* pTarget = pVisualizationTarget->Get();

				context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
				context.SetPipelineState(m_pTiledVisualizeLightsPSO);

				context.BindRootCBV(1, Renderer::GetViewUniforms(pView, pTarget));
				context.BindResources(2, pTarget->GetUAV());
				context.BindResources(3, {
					pSceneDepth->Get()->GetSRV(),
					lightCullData.pLightListOpaque->Get()->GetSRV(),
					});

				context.Dispatch(ComputeUtils::GetNumThreadGroups(
					pTarget->GetWidth(), 16,
					pTarget->GetHeight(), 16));
			});

	return pVisualizationTarget;
}

RGTexture* LightCulling::VisualizeLightDensity(RGGraph& graph, const SceneView* pView, RGTexture* pSceneDepth, const LightCull3DData& lightCullData)
{
	RGTexture* pVisualizationTarget = graph.Create("Light Density Visualization", TextureDesc::Create2D(pSceneDepth->GetDesc().Width, pSceneDepth->GetDesc().Height, ResourceFormat::RGBA8_UNORM, 1));

	RGBuffer* pLightGrid = lightCullData.pLightGrid;
	Vector2 lightGridParams = lightCullData.LightGridParams;
	Vector3i clusterCount = lightCullData.ClusterCount;

	graph.AddPass("Visualize Light Density", RGPassFlag::Compute)
		.Read({ pSceneDepth, pLightGrid })
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

				context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
				context.SetPipelineState(m_pClusteredVisualizeLightsPSO);

				context.BindRootCBV(0, constantBuffer);
				context.BindRootCBV(1, Renderer::GetViewUniforms(pView, pTarget));
				context.BindResources(2, pTarget->GetUAV());
				context.BindResources(3, {
					pSceneDepth->Get()->GetSRV(),
					pLightGrid->Get()->GetSRV(),
					});

				context.Dispatch(ComputeUtils::GetNumThreadGroups(
					pTarget->GetWidth(), 16,
					pTarget->GetHeight(), 16));
			});

	return pVisualizationTarget;
}
