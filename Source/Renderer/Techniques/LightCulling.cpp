#include "stdafx.h"
#include "LightCulling.h"
#include "Core/Profiler.h"
#include "Core/ConsoleVariables.h"
#include "RHI/PipelineState.h"
#include "RHI/RootSignature.h"
#include "RHI/Buffer.h"
#include "RHI/Device.h"
#include "RHI/CommandContext.h"
#include "RHI/Texture.h"
#include "RHI/ResourceViews.h"
#include "Renderer/Renderer.h"
#include "Renderer/Light.h"
#include "RenderGraph/RenderGraph.h"
#include "Scene/World.h"

// Clustered
static constexpr int gLightClusterTexelSize = 64;
static constexpr int gLightClustersNumZ = 32;
static constexpr int gClusteredLightingMaxLights = 1024;
static_assert(gClusteredLightingMaxLights % 32 == 0);

// Tiled
static constexpr int gTiledLightingTileSize = 8;
static constexpr int gTiledMaxLights = 1024;
static_assert(gTiledMaxLights % 32 == 0);

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

void LightCulling::ComputeClusteredLightCulling(RGGraph& graph, const RenderView* pView, LightCull3DData& cullData)
{
	RG_GRAPH_SCOPE("Clustered Light Culling", graph);

	cullData.ClusterCount.x = Math::DivideAndRoundUp(pView->GetDimensions().x, gLightClusterTexelSize);
	cullData.ClusterCount.y = Math::DivideAndRoundUp(pView->GetDimensions().y, gLightClusterTexelSize);
	cullData.ClusterCount.z = gLightClustersNumZ;
	float nearZ = pView->NearPlane;
	float farZ = pView->FarPlane;
	float n = Math::Min(nearZ, farZ);
	float f = Math::Max(nearZ, farZ);
	cullData.LightGridParams.x = (float)gLightClustersNumZ / log(f / n);
	cullData.LightGridParams.y = ((float)gLightClustersNumZ * log(n)) / log(f / n);
	cullData.ClusterSize = gLightClusterTexelSize;

	uint32 totalClusterCount = cullData.ClusterCount.x * cullData.ClusterCount.y * cullData.ClusterCount.z;

	cullData.pLightGrid = graph.Create("Light Index Grid", BufferDesc::CreateTyped(gClusteredLightingMaxLights / 32 * totalClusterCount, ResourceFormat::R32_UINT));

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

	const Renderer* pRenderer = pView->pRenderer;
	uint32 precomputedLightDataSize = sizeof(PrecomputedLightData) * pRenderer->GetNumLights();

	RGBuffer* pPrecomputeData = graph.Create("Precompute Light Data", BufferDesc::CreateStructured(pRenderer->GetNumLights(), sizeof(PrecomputedLightData)));
	graph.AddPass("Precompute Light View Data", RGPassFlag::Copy)
		.Write(pPrecomputeData)
		.Bind([=](CommandContext& context, const RGResources& resources)
			{
				ScratchAllocation allocation = context.AllocateScratch(precomputedLightDataSize);
				PrecomputedLightData* pLightData = &allocation.As<PrecomputedLightData>();

				const Matrix& viewMatrix = pView->WorldToView;
				auto light_view = pView->pWorld->Registry.view<const Transform, const Light>();
				light_view.each([&](const Transform& transform, const Light& light)
					{
						PrecomputedLightData& data = *pLightData++;
						data.ViewSpacePosition = Vector3::Transform(transform.Position, viewMatrix);
						data.ViewSpaceDirection = Vector3::TransformNormal(Vector3::Transform(Vector3::Forward, transform.Rotation), viewMatrix);
						data.SpotCosAngle = cos(light.OuterConeAngle / 2.0f);
						data.SpotSinAngle = sin(light.OuterConeAngle / 2.0f);
						data.Range = light.Range;
						data.IsSpot = light.Type == LightType::Spot;
						data.IsPoint = light.Type == LightType::Point;
						data.IsDirectional = light.Type == LightType::Directional;
					});
				context.CopyBuffer(allocation.pBackingResource, resources.Get(pPrecomputeData), precomputedLightDataSize, allocation.Offset, 0);
			});

	graph.AddPass("Cull Lights", RGPassFlag::Compute)
		.Read(pPrecomputeData)
		.Write({ cullData.pLightGrid })
		.Bind([=](CommandContext& context, const RGResources& resources)
			{
				context.SetPipelineState(m_pClusteredCullPSO);
				context.SetComputeRootSignature(GraphicsCommon::pCommonRS);

				// Clear the light grid because we're accumulating the light count in the shader
				Buffer* pLightGrid = resources.Get(cullData.pLightGrid);
				context.ClearBufferUInt(pLightGrid);

				Renderer::BindViewUniforms(context, *pView);

				struct
				{
					Vector4i ClusterDimensions;
					Vector2i ClusterSize;

				} constantBuffer;

				constantBuffer.ClusterSize = Vector2i(gLightClusterTexelSize, gLightClusterTexelSize);
				constantBuffer.ClusterDimensions = Vector4i(cullData.ClusterCount.x, cullData.ClusterCount.y, cullData.ClusterCount.z, 0);

				context.BindRootCBV(BindingSlot::PerInstance, constantBuffer);

				context.BindResources(BindingSlot::UAV, {
					resources.GetUAV(cullData.pLightGrid),
					});
				context.BindResources(BindingSlot::SRV, {
					resources.GetSRV(pPrecomputeData)
					});

				context.Dispatch(
					ComputeUtils::GetNumThreadGroups(
						cullData.ClusterCount.x, 4,
						cullData.ClusterCount.y, 4,
						cullData.ClusterCount.z, 4)
				);
			});
}

void LightCulling::ComputeTiledLightCulling(RGGraph& graph, const RenderView* pView, SceneTextures& sceneTextures, LightCull2DData& cullResources)
{
	RG_GRAPH_SCOPE("Tiled Light Culling", graph);

	uint32 tilesX = Math::DivideAndRoundUp((uint32)pView->Viewport.GetWidth(), gTiledLightingTileSize);
	uint32 tilesY = Math::DivideAndRoundUp((uint32)pView->Viewport.GetHeight(), gTiledLightingTileSize);
	uint32 lightListElements = tilesX * tilesY * (gTiledMaxLights / 32);

	cullResources.pLightListOpaque = graph.Create("Light List - Opaque", BufferDesc::CreateTyped(lightListElements, ResourceFormat::R32_UINT));
	cullResources.pLightListTransparent = graph.Create("Light List - Transparant", BufferDesc::CreateTyped(lightListElements, ResourceFormat::R32_UINT));

	struct PrecomputedLightData
	{
		Vector3 SphereViewPosition;
		float SphereRadius;
	};

	const Renderer* pRenderer = pView->pRenderer;

	RGBuffer* pPrecomputeData = graph.Create("Precompute Light Data", BufferDesc::CreateStructured(pRenderer->GetNumLights(), sizeof(PrecomputedLightData)));
	graph.AddPass("Precompute Light View Data", RGPassFlag::Copy)
		.Write(pPrecomputeData)
		.Bind([=](CommandContext& context, const RGResources& resources)
			{
				uint32 precomputedLightDataSize = sizeof(PrecomputedLightData) * pRenderer->GetNumLights();
				ScratchAllocation allocation = context.AllocateScratch(precomputedLightDataSize);
				PrecomputedLightData* pLightData = &allocation.As<PrecomputedLightData>();

				const Matrix& viewMatrix = pView->WorldToView;
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
							if (light.OuterConeAngle > Math::PI_DIV_2)
							{
								data.SphereRadius = light.Range * tanf(light.OuterConeAngle * 0.5f);
								data.SphereViewPosition = Vector3::Transform(transform.Position + Vector3::TransformNormal(Vector3::Forward * light.Range, Matrix::CreateFromQuaternion(transform.Rotation)), viewMatrix);
							}
							else
							{
								data.SphereRadius = light.Range * 0.5f / powf(cosf(light.OuterConeAngle * 0.5f), 2.0f);
								data.SphereViewPosition = Vector3::Transform(transform.Position + Vector3::TransformNormal(Vector3::Forward * data.SphereRadius, Matrix::CreateFromQuaternion(transform.Rotation)), viewMatrix);
							}
						}
					});
				context.CopyBuffer(allocation.pBackingResource, resources.Get(pPrecomputeData), precomputedLightDataSize, allocation.Offset, 0);
			});

	graph.AddPass("2D Light Culling", RGPassFlag::Compute)
		.Read({ sceneTextures.pDepth, pPrecomputeData })
		.Write({ cullResources.pLightListOpaque, cullResources.pLightListTransparent })
		.Bind([=](CommandContext& context, const RGResources& resources)
			{
				Texture* pDepth = resources.Get(sceneTextures.pDepth);

				context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
				context.SetPipelineState(m_pTiledCullPSO);

				Renderer::BindViewUniforms(context, *pView);

				context.BindResources(BindingSlot::UAV, {
					resources.GetUAV(cullResources.pLightListOpaque),
					resources.GetUAV(cullResources.pLightListTransparent),
					});
				context.BindResources(BindingSlot::SRV, {
					pDepth->GetSRV(),
					resources.GetSRV(pPrecomputeData),
					});

				context.Dispatch(ComputeUtils::GetNumThreadGroups(
					pDepth->GetWidth(), gTiledLightingTileSize,
					pDepth->GetHeight(), gTiledLightingTileSize
				));
			});
}

RGTexture* LightCulling::VisualizeLightDensity(RGGraph& graph, const RenderView* pView, RGTexture* pSceneDepth, const LightCull2DData& lightCullData)
{
	RGTexture* pVisualizationTarget = graph.Create("Light Density Visualization", TextureDesc::Create2D(pSceneDepth->GetDesc().Width, pSceneDepth->GetDesc().Height, ResourceFormat::RGBA8_UNORM, 1));

	graph.AddPass("Visualize Light Density", RGPassFlag::Compute)
		.Read({ pSceneDepth, lightCullData.pLightListOpaque })
		.Write(pVisualizationTarget)
		.Bind([=](CommandContext& context, const RGResources& resources)
			{
				Texture* pTarget = resources.Get(pVisualizationTarget);

				context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
				context.SetPipelineState(m_pTiledVisualizeLightsPSO);

				Renderer::BindViewUniforms(context, *pView);
				context.BindResources(BindingSlot::UAV, pTarget->GetUAV());
				context.BindResources(BindingSlot::SRV, {
					resources.GetSRV(pSceneDepth),
					resources.GetSRV(lightCullData.pLightListOpaque),
					});

				context.Dispatch(ComputeUtils::GetNumThreadGroups(
					pTarget->GetWidth(), 16,
					pTarget->GetHeight(), 16));
			});

	return pVisualizationTarget;
}

RGTexture* LightCulling::VisualizeLightDensity(RGGraph& graph, const RenderView* pView, RGTexture* pSceneDepth, const LightCull3DData& lightCullData)
{
	RGTexture* pVisualizationTarget = graph.Create("Light Density Visualization", TextureDesc::Create2D(pSceneDepth->GetDesc().Width, pSceneDepth->GetDesc().Height, ResourceFormat::RGBA8_UNORM, 1));

	RGBuffer* pLightGrid = lightCullData.pLightGrid;
	Vector2 lightGridParams = lightCullData.LightGridParams;
	Vector3i clusterCount = lightCullData.ClusterCount;

	graph.AddPass("Visualize Light Density", RGPassFlag::Compute)
		.Read({ pSceneDepth, pLightGrid })
		.Write(pVisualizationTarget)
		.Bind([=](CommandContext& context, const RGResources& resources)
			{
				Texture* pTarget = resources.Get(pVisualizationTarget);

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

				Renderer::BindViewUniforms(context, *pView);
				context.BindRootCBV(BindingSlot::PerInstance, constantBuffer);
				context.BindResources(BindingSlot::UAV, pTarget->GetUAV());
				context.BindResources(BindingSlot::SRV, {
					resources.GetSRV(pSceneDepth),
					resources.GetSRV(pLightGrid),
					});

				context.Dispatch(ComputeUtils::GetNumThreadGroups(
					pTarget->GetWidth(), 16,
					pTarget->GetHeight(), 16));
			});

	return pVisualizationTarget;
}
