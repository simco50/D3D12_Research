#include "stdafx.h"
#include "LightCulling.h"
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

LightCulling::LightCulling(GraphicsDevice* pDevice)
	: m_pDevice(pDevice)
{
	m_pCommonRS = new RootSignature(pDevice);
	m_pCommonRS->AddRootConstants(0, 8);
	m_pCommonRS->AddRootCBV(100);
	m_pCommonRS->AddDescriptorTable(0, 8, D3D12_DESCRIPTOR_RANGE_TYPE_UAV);
	m_pCommonRS->AddDescriptorTable(0, 8, D3D12_DESCRIPTOR_RANGE_TYPE_SRV);
	m_pCommonRS->Finalize("Light Density Visualization");

	// Clustered
	m_pClusteredCullPSO = pDevice->CreateComputePipeline(m_pCommonRS, "ClusteredLightCulling.hlsl", "LightCulling");
	m_pClusteredVisualizeLightsPSO = pDevice->CreateComputePipeline(m_pCommonRS, "VisualizeLightCount.hlsl", "DebugLightDensityCS", { "CLUSTERED_FORWARD" });

	// Tiled
	m_pTiledCullPSO = m_pDevice->CreateComputePipeline(m_pCommonRS, "LightCulling.hlsl", "CSMain");
	m_pTiledVisualizeLightsPSO = m_pDevice->CreateComputePipeline(m_pCommonRS, "VisualizeLightCount.hlsl", "DebugLightDensityCS", { "TILED_FORWARD" });
}

LightCulling::~LightCulling()
{
}

void LightCulling::ComputeClusteredLightCulling(RGGraph& graph, const SceneView* pView, LightCull3DData& cullData)
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
				ScratchAllocation allocation = context.AllocateScratch(precomputedLightDataSize);
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

void LightCulling::ComputeTiledLightCulling(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures, LightCull2DData& resources)
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
				ScratchAllocation allocation = context.AllocateScratch(precomputedLightDataSize);
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

RGTexture* LightCulling::VisualizeLightDensity(RGGraph& graph, const SceneView* pView, RGTexture* pSceneDepth, const LightCull2DData& lightCullData)
{
	RGTexture* pVisualizationTarget = graph.Create("Light Density Visualization", TextureDesc::Create2D(pSceneDepth->GetDesc().Width, pSceneDepth->GetDesc().Height, ResourceFormat::RGBA8_UNORM, 1));
	RGTexture* pLightGridOpaque = lightCullData.pLightGridOpaque;

	graph.AddPass("Visualize Light Density", RGPassFlag::Compute)
		.Read({ pSceneDepth, pLightGridOpaque })
		.Write(pVisualizationTarget)
		.Bind([=](CommandContext& context)
			{
				Texture* pTarget = pVisualizationTarget->Get();

				context.SetComputeRootSignature(m_pCommonRS);
				context.SetPipelineState(m_pTiledVisualizeLightsPSO);

				context.BindRootCBV(1, Renderer::GetViewUniforms(pView, pTarget));
				context.BindResources(2, pTarget->GetUAV());
				context.BindResources(3, {
					pSceneDepth->Get()->GetSRV(),
					pLightGridOpaque->Get()->GetSRV(),
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

				context.SetComputeRootSignature(m_pCommonRS);
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
