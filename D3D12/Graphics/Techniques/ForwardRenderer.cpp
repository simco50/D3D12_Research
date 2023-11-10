#include "stdafx.h"
#include "ForwardRenderer.h"
#include "Graphics/RHI/PipelineState.h"
#include "Graphics/RHI/RootSignature.h"
#include "Graphics/RHI/Buffer.h"
#include "Graphics/RHI/Graphics.h"
#include "Graphics/RHI/CommandContext.h"
#include "Graphics/RHI/Texture.h"
#include "Graphics/RHI/ResourceViews.h"
#include "Graphics/Techniques/LightCulling.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Core/Profiler.h"
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
static constexpr int gTiledLightingTileSize = 16;

ForwardRenderer::ForwardRenderer(GraphicsDevice* pDevice)
	: m_pDevice(pDevice)
{
	m_pForwardRS = new RootSignature(pDevice);
	m_pForwardRS->AddRootConstants(0, 6);
	m_pForwardRS->AddRootCBV(1);
	m_pForwardRS->AddRootCBV(100);
	m_pForwardRS->AddDescriptorTable(0, 8, D3D12_DESCRIPTOR_RANGE_TYPE_SRV);
	m_pForwardRS->Finalize("Forward");

	// Clustered
	{
		//Opaque
		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pForwardRS);
		psoDesc.SetBlendMode(BlendMode::Replace, false);
		psoDesc.SetAmplificationShader("ForwardShading.hlsl", "ASMain", { "CLUSTERED_FORWARD" });
		psoDesc.SetMeshShader("ForwardShading.hlsl", "MSMain", { "CLUSTERED_FORWARD" });
		psoDesc.SetPixelShader("ForwardShading.hlsl", "ShadePS", { "CLUSTERED_FORWARD" });
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_EQUAL);
		psoDesc.SetDepthWrite(false);

		psoDesc.SetRenderTargetFormats(GraphicsCommon::GBufferFormat, GraphicsCommon::DepthStencilFormat, 1);
		psoDesc.SetName("Forward - Opaque");
		m_pClusteredForwardPSO = pDevice->CreatePipeline(psoDesc);

		//Opaque Masked
		psoDesc.SetName("Forward - Opaque Masked");
		psoDesc.SetCullMode(D3D12_CULL_MODE_NONE);
		m_pClusteredForwardMaskedPSO = pDevice->CreatePipeline(psoDesc);

		//Transparant
		psoDesc.SetName("Forward - Transparent");
		psoDesc.SetBlendMode(BlendMode::Alpha, false);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
		m_pClusteredForwardAlphaBlendPSO = pDevice->CreatePipeline(psoDesc);
	}

	// Tiled
	{
		//Opaque
		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pForwardRS);
		psoDesc.SetAmplificationShader("ForwardShading.hlsl", "ASMain", { "TILED_FORWARD" });
		psoDesc.SetMeshShader("ForwardShading.hlsl", "MSMain", { "TILED_FORWARD" });
		psoDesc.SetPixelShader("ForwardShading.hlsl", "ShadePS", { "TILED_FORWARD" });
		psoDesc.SetRenderTargetFormats(GraphicsCommon::GBufferFormat, GraphicsCommon::DepthStencilFormat, 1);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_EQUAL);
		psoDesc.SetDepthWrite(false);

		psoDesc.SetName("Forward - Opaque");
		m_pTiledForwardPSO = m_pDevice->CreatePipeline(psoDesc);

		//Alpha Mask
		psoDesc.SetCullMode(D3D12_CULL_MODE_NONE);
		psoDesc.SetName("Forward - Opaque Masked");
		m_pTiledForwardMaskedPSO = m_pDevice->CreatePipeline(psoDesc);

		//Transparant
		psoDesc.SetBlendMode(BlendMode::Alpha, false);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
		psoDesc.SetName("Forward - Transparent");
		m_pTiledForwardAlphaBlendPSO = m_pDevice->CreatePipeline(psoDesc);
	}

}

ForwardRenderer::~ForwardRenderer()
{
}

void ForwardRenderer::RenderForwardClustered(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures, const LightCull3DData& lightCullData, RGTexture* pFogTexture, bool translucentOnly)
{
	RenderTargetLoadAction rtLoadOp = translucentOnly ? RenderTargetLoadAction::Load : RenderTargetLoadAction::DontCare;

	graph.AddPass("Forward Shading", RGPassFlag::Raster)
		.Read({ sceneTextures.pAmbientOcclusion, sceneTextures.pPreviousColor, pFogTexture, sceneTextures.pDepth })
		.Read({ lightCullData.pLightGrid, lightCullData.pLightIndexGrid })
		.DepthStencil(sceneTextures.pDepth, RenderTargetLoadAction::Load, false)
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
					lightCullData.pLightIndexGrid->Get()->GetSRV(),
					lightCullData.pLightGrid->Get()->GetSRV(),
					});

				if (!translucentOnly)
				{
					{
						PROFILE_GPU_SCOPE(context.GetCommandList(), "Opaque");
						context.SetPipelineState(m_pClusteredForwardPSO);
						Renderer::DrawScene(context, pView, Batch::Blending::Opaque);
					}
					{
						PROFILE_GPU_SCOPE(context.GetCommandList(), "Opaque - Masked");
						context.SetPipelineState(m_pClusteredForwardMaskedPSO);
						Renderer::DrawScene(context, pView, Batch::Blending::AlphaMask);
					}
				}
				{
					PROFILE_GPU_SCOPE(context.GetCommandList(), "Transparant");
					context.SetPipelineState(m_pClusteredForwardAlphaBlendPSO);
					Renderer::DrawScene(context, pView, Batch::Blending::AlphaBlend);
				}
			});
}

void ForwardRenderer::RenderForwardTiled(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures, const LightCull2DData& lightCullData, RGTexture* pFogTexture)
{
	graph.AddPass("Forward Shading", RGPassFlag::Raster)
		.Read({ sceneTextures.pAmbientOcclusion, sceneTextures.pPreviousColor, pFogTexture })
		.Read({ lightCullData.pLightListOpaque, lightCullData.pLightListTransparent })
		.DepthStencil(sceneTextures.pDepth, RenderTargetLoadAction::Load, false)
		.RenderTarget(sceneTextures.pColorTarget, RenderTargetLoadAction::DontCare)
		.RenderTarget(sceneTextures.pNormals, RenderTargetLoadAction::DontCare)
		.RenderTarget(sceneTextures.pRoughness, RenderTargetLoadAction::DontCare)
		.Bind([=](CommandContext& context)
			{
				context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				context.SetGraphicsRootSignature(m_pForwardRS);

				context.BindRootCBV(2, Renderer::GetViewUniforms(pView, sceneTextures.pColorTarget->Get()));

				{
					context.BindResources(3, {
						sceneTextures.pAmbientOcclusion->Get()->GetSRV(),
						sceneTextures.pDepth->Get()->GetSRV(),
						sceneTextures.pPreviousColor->Get()->GetSRV(),
						pFogTexture->Get()->GetSRV(),
						lightCullData.pLightListOpaque->Get()->GetSRV(),
						});

					{
						PROFILE_GPU_SCOPE(context.GetCommandList(), "Opaque");
						context.SetPipelineState(m_pTiledForwardPSO);
						Renderer::DrawScene(context, pView, Batch::Blending::Opaque);
					}

					{
						PROFILE_GPU_SCOPE(context.GetCommandList(), "Opaque Masked");
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
						lightCullData.pLightListTransparent->Get()->GetSRV(),
						});

					{
						PROFILE_GPU_SCOPE(context.GetCommandList(), "Transparant");
						context.SetPipelineState(m_pTiledForwardAlphaBlendPSO);
						Renderer::DrawScene(context, pView, Batch::Blending::AlphaBlend);
					}
				}
			});
}
