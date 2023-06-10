#include "stdafx.h"
#include "VolumetricFog.h"
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
#include "Graphics/Techniques/ForwardRenderer.h"
#include "Core/ConsoleVariables.h"

static constexpr int gLightClusterTexelSize = 64;
static constexpr int gLightClustersNumZ = 32;
static constexpr int gMaxLightsPerCluster = 32;

static constexpr int gVolumetricFroxelTexelSize = 8;
static constexpr int gVolumetricNumZSlices = 128;

VolumetricFog::VolumetricFog(GraphicsDevice* pDevice)
	: m_pDevice(pDevice)
{
	m_pCommonRS = new RootSignature(pDevice);
	m_pCommonRS->AddRootCBV(0);
	m_pCommonRS->AddRootCBV(100);
	m_pCommonRS->AddDescriptorTable(0, 8, D3D12_DESCRIPTOR_RANGE_TYPE_UAV);
	m_pCommonRS->AddDescriptorTable(0, 8, D3D12_DESCRIPTOR_RANGE_TYPE_SRV);
	m_pCommonRS->Finalize("Light Density Visualization");

	// Volumetric fog
	m_pInjectVolumeLightPSO = pDevice->CreateComputePipeline(m_pCommonRS, "VolumetricFog.hlsl", "InjectFogLightingCS");
	m_pAccumulateVolumeLightPSO = pDevice->CreateComputePipeline(m_pCommonRS, "VolumetricFog.hlsl", "AccumulateFogCS");
}

VolumetricFog::~VolumetricFog()
{
}

RGTexture* VolumetricFog::RenderFog(RGGraph& graph, const SceneView* pView, const LightCull3DData& lightCullData, VolumetricFogData& fogData)
{
	RG_GRAPH_SCOPE("Volumetric Lighting", graph);

	TextureDesc volumeDesc = TextureDesc::Create3D(
		Math::DivideAndRoundUp((uint32)pView->GetDimensions().x, gVolumetricFroxelTexelSize),
		Math::DivideAndRoundUp((uint32)pView->GetDimensions().y, gVolumetricFroxelTexelSize),
		gVolumetricNumZSlices,
		ResourceFormat::RGBA16_FLOAT);

	RGTexture* pSourceVolume = graph.TryImport(fogData.pFogHistory, GraphicsCommon::GetDefaultTexture(DefaultTexture::Black3D));
	RGTexture* pTargetVolume = graph.Create("Fog Target", volumeDesc);
	graph.Export(pTargetVolume, &fogData.pFogHistory);

	graph.AddPass("Inject Volume Lights", RGPassFlag::Compute)
		.Read({ pSourceVolume, lightCullData.pLightGrid, lightCullData.pLightIndexGrid })
		.Write(pTargetVolume)
		.Bind([=](CommandContext& context)
			{
				Texture* pTarget = pTargetVolume->Get();

				context.SetComputeRootSignature(m_pCommonRS);
				context.SetPipelineState(m_pInjectVolumeLightPSO);

				struct
				{
					Vector3i ClusterDimensions;
					float Jitter;
					Vector3 InvClusterDimensions;
					float LightClusterSizeFactor;
					Vector2 LightGridParams;
					Vector2i LightClusterDimensions;
					float MinBlendFactor;
				} params;

				params.ClusterDimensions = Vector3i(volumeDesc.Width, volumeDesc.Height, volumeDesc.DepthOrArraySize);
				params.InvClusterDimensions = Vector3(1.0f / volumeDesc.Width, 1.0f / volumeDesc.Height, 1.0f / volumeDesc.DepthOrArraySize);
				constexpr Math::HaltonSequence<32, 2> halton;
				params.Jitter = halton[pView->FrameIndex & 31];
				params.LightClusterSizeFactor = (float)gVolumetricFroxelTexelSize / gLightClusterTexelSize;
				params.LightGridParams = lightCullData.LightGridParams;
				params.LightClusterDimensions = Vector2i(lightCullData.ClusterCount.x, lightCullData.ClusterCount.y);
				params.MinBlendFactor = pView->CameraCut ? 1.0f : 0.0f;

				context.BindRootCBV(0, params);
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

	RGTexture* pFinalVolumeFog = graph.Create("Volumetric Fog", volumeDesc);

	graph.AddPass("Accumulate Volume Fog", RGPassFlag::Compute)
		.Read({ pTargetVolume, lightCullData.pLightGrid, lightCullData.pLightIndexGrid })
		.Write(pFinalVolumeFog)
		.Bind([=](CommandContext& context)
			{
				Texture* pFinalFog = pFinalVolumeFog->Get();

				context.SetComputeRootSignature(m_pCommonRS);
				context.SetPipelineState(m_pAccumulateVolumeLightPSO);

				struct
				{
					Vector3i ClusterDimensions;
					uint32 : 32;
					Vector3 InvClusterDimensions;
					uint32 : 32;
				} params;
				params.ClusterDimensions = Vector3i(volumeDesc.Width, volumeDesc.Height, volumeDesc.DepthOrArraySize);
				params.InvClusterDimensions = Vector3(1.0f / volumeDesc.Width, 1.0f / volumeDesc.Height, 1.0f / volumeDesc.DepthOrArraySize);

				context.BindRootCBV(0, params);
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
