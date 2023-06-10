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
#include "Graphics/Light.h"
#include "Core/ConsoleVariables.h"

static constexpr int gLightClusterTexelSize = 64;
static constexpr int gLightClustersNumZ = 32;
static constexpr int gMaxLightsPerCluster = 32;

static constexpr int gVolumetricFroxelTexelSize = 8;
static constexpr int gVolumetricNumZSlices = 128;

ClusteredForward::ClusteredForward(GraphicsDevice* pDevice)
	: m_pDevice(pDevice)
{
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

	m_pVisualizeLightsPSO = pDevice->CreateComputePipeline(m_pCommonRS, "VisualizeLightCount.hlsl", "DebugLightDensityCS", { "CLUSTERED_FORWARD" });
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
				context.SetPipelineState(m_pLightCullingPSO);
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

void ClusteredForward::RenderBasePass(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures, const LightCull3DData& lightCullData, RGTexture* pFogTexture, bool translucentOnly)
{
	static constexpr bool useMeshShader = false;
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

				if (!translucentOnly)
				{
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
