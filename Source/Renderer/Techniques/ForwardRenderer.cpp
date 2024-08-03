#include "stdafx.h"
#include "ForwardRenderer.h"
#include "Core/ConsoleVariables.h"
#include "Core/Profiler.h"
#include "RHI/PipelineState.h"
#include "RHI/RootSignature.h"
#include "RHI/Buffer.h"
#include "RHI/Device.h"
#include "RHI/CommandContext.h"
#include "RHI/Texture.h"
#include "RHI/ResourceViews.h"
#include "Renderer/Light.h"
#include "Renderer/Renderer.h"
#include "Renderer/Techniques/LightCulling.h"
#include "RenderGraph/RenderGraph.h"

ForwardRenderer::ForwardRenderer(GraphicsDevice* pDevice)
	: m_pDevice(pDevice)
{
	// Clustered
	{
		//Opaque
		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(GraphicsCommon::pCommonRS);
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
		psoDesc.SetRootSignature(GraphicsCommon::pCommonRS);
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

void ForwardRenderer::RenderForwardClustered(RGGraph& graph, const RenderView* pView, SceneTextures& sceneTextures, const LightCull3DData& lightCullData, RGTexture* pFogTexture, RGTexture* pAO, bool translucentOnly)
{
	graph.AddPass("Forward Shading", RGPassFlag::Raster)
		.Read({ sceneTextures.pDepth })
		.Read({ pAO, sceneTextures.pPreviousColor, pFogTexture, sceneTextures.pDepth })
		.Read({ lightCullData.pLightGrid })
		.DepthStencil(sceneTextures.pDepth, RenderPassDepthFlags::ReadOnly)
		.RenderTarget(sceneTextures.pColorTarget)
		.RenderTarget(sceneTextures.pNormals)
		.RenderTarget(sceneTextures.pRoughness)
		.Bind([=](CommandContext& context, const RGResources& resources)
			{
				context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				context.SetGraphicsRootSignature(GraphicsCommon::pCommonRS);

				struct
				{
					Vector4i ClusterDimensions;
					Vector2i ClusterSize;
					Vector2 LightGridParams;
				} frameData;

				frameData.ClusterDimensions = Vector4i(lightCullData.ClusterCount.x, lightCullData.ClusterCount.y, lightCullData.ClusterCount.z, 0);
				frameData.ClusterSize = Vector2i(lightCullData.ClusterSize, lightCullData.ClusterSize);
				frameData.LightGridParams = lightCullData.LightGridParams;

				Renderer::BindViewUniforms(context, *pView);
				context.BindRootCBV(BindingSlot::PerPass, frameData);

				context.BindResources(BindingSlot::SRV, {
					resources.GetSRV(pAO),
					resources.GetSRV(sceneTextures.pDepth),
					resources.GetSRV(sceneTextures.pPreviousColor),
					resources.GetSRV(pFogTexture),
					resources.GetSRV(lightCullData.pLightGrid),
					});

				if (!translucentOnly)
				{
					{
						PROFILE_GPU_SCOPE(context.GetCommandList(), "Opaque");
						context.SetPipelineState(m_pClusteredForwardPSO);
						Renderer::DrawScene(context, *pView, Batch::Blending::Opaque);
					}
					{
						PROFILE_GPU_SCOPE(context.GetCommandList(), "Opaque - Masked");
						context.SetPipelineState(m_pClusteredForwardMaskedPSO);
						Renderer::DrawScene(context, *pView, Batch::Blending::AlphaMask);
					}
				}
				{
					PROFILE_GPU_SCOPE(context.GetCommandList(), "Transparant");
					context.SetPipelineState(m_pClusteredForwardAlphaBlendPSO);
					Renderer::DrawScene(context, *pView, Batch::Blending::AlphaBlend);
				}
			});
}

void ForwardRenderer::RenderForwardTiled(RGGraph& graph, const RenderView* pView, SceneTextures& sceneTextures, const LightCull2DData& lightCullData, RGTexture* pFogTexture, RGTexture* pAO)
{
	graph.AddPass("Forward Shading", RGPassFlag::Raster)
		.Read({ sceneTextures.pDepth })
		.Read({ pAO, sceneTextures.pPreviousColor, pFogTexture })
		.Read({ lightCullData.pLightListOpaque, lightCullData.pLightListTransparent })
		.DepthStencil(sceneTextures.pDepth, RenderPassDepthFlags::ReadOnly)
		.RenderTarget(sceneTextures.pColorTarget)
		.RenderTarget(sceneTextures.pNormals)
		.RenderTarget(sceneTextures.pRoughness)
		.Bind([=](CommandContext& context, const RGResources& resources)
			{
				context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				context.SetGraphicsRootSignature(GraphicsCommon::pCommonRS);

				Renderer::BindViewUniforms(context, *pView);

				{
					context.BindResources(BindingSlot::SRV, {
						resources.GetSRV(pAO),
						resources.GetSRV(sceneTextures.pDepth),
						resources.GetSRV(sceneTextures.pPreviousColor),
						resources.GetSRV(pFogTexture),
						resources.GetSRV(lightCullData.pLightListOpaque),
						});

					{
						PROFILE_GPU_SCOPE(context.GetCommandList(), "Opaque");
						context.SetPipelineState(m_pTiledForwardPSO);
						Renderer::DrawScene(context, *pView, Batch::Blending::Opaque);
					}

					{
						PROFILE_GPU_SCOPE(context.GetCommandList(), "Opaque Masked");
						context.SetPipelineState(m_pTiledForwardMaskedPSO);
						Renderer::DrawScene(context, *pView, Batch::Blending::AlphaMask);
					}
				}

				{
					context.BindResources(BindingSlot::SRV, {
						resources.GetSRV(pAO),
						resources.GetSRV(sceneTextures.pDepth),
						resources.GetSRV(sceneTextures.pPreviousColor),
						resources.GetSRV(pFogTexture),
						resources.GetSRV(lightCullData.pLightListTransparent),
						});

					{
						PROFILE_GPU_SCOPE(context.GetCommandList(), "Transparant");
						context.SetPipelineState(m_pTiledForwardAlphaBlendPSO);
						Renderer::DrawScene(context, *pView, Batch::Blending::AlphaBlend);
					}
				}
			});
}
