#include "stdafx.h"
#include "TiledForward.h"
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

static constexpr int MAX_LIGHT_DENSITY = 72000;
static constexpr int FORWARD_PLUS_BLOCK_SIZE = 16;

TiledForward::TiledForward(GraphicsDevice* pDevice)
	: m_pDevice(pDevice)
{
	m_pCommonRS = new RootSignature(m_pDevice);
	m_pCommonRS->AddRootConstants(0, 6);
	m_pCommonRS->AddConstantBufferView(100);
	m_pCommonRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 6);
	m_pCommonRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 6);
	m_pCommonRS->Finalize("Common");

	{
		constexpr ResourceFormat formats[] = {
			ResourceFormat::RGBA16_FLOAT,
			ResourceFormat::RG16_FLOAT,
			ResourceFormat::R8_UNORM,
		};

		//Opaque
		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pCommonRS);
		psoDesc.SetVertexShader("Diffuse.hlsl", "VSMain", { "TILED_FORWARD" });
		psoDesc.SetPixelShader("Diffuse.hlsl", "PSMain", { "TILED_FORWARD" });
		psoDesc.SetRenderTargetFormats(formats, GraphicsCommon::DepthStencilFormat, 1);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_EQUAL);
		psoDesc.SetDepthWrite(false);
		psoDesc.SetName("Forward Pass - Opaque");
		m_pDiffusePSO = m_pDevice->CreatePipeline(psoDesc);

		//Alpha Mask
		psoDesc.SetCullMode(D3D12_CULL_MODE_NONE);
		psoDesc.SetName("Forward Pass - Opaque Masked");
		m_pDiffuseMaskedPSO = m_pDevice->CreatePipeline(psoDesc);

		//Transparant
		psoDesc.SetBlendMode(BlendMode::Alpha, false);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
		psoDesc.SetName("Forward Pass - Transparent");
		m_pDiffuseAlphaPSO = m_pDevice->CreatePipeline(psoDesc);
	}

	m_pComputeLightCullPSO = m_pDevice->CreateComputePipeline(m_pCommonRS, "LightCulling.hlsl", "CSMain");
	m_pVisualizeLightsPSO = m_pDevice->CreateComputePipeline(m_pCommonRS, "VisualizeLightCount.hlsl", "DebugLightDensityCS", { "TILED_FORWARD" });
}

void TiledForward::ComputeLightCulling(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures, LightCull2DData& resources)
{
	int frustumCountX = Math::DivideAndRoundUp(pView->GetDimensions().x, FORWARD_PLUS_BLOCK_SIZE);
	int frustumCountY = Math::DivideAndRoundUp(pView->GetDimensions().y, FORWARD_PLUS_BLOCK_SIZE);
	resources.pLightGridOpaque = graph.Create("Light Grid - Opaque", TextureDesc::Create2D(frustumCountX, frustumCountY, ResourceFormat::RG32_UINT));
	resources.pLightGridTransparant = graph.Create("Light Grid - Transparant", TextureDesc::Create2D(frustumCountX, frustumCountY, ResourceFormat::RG32_UINT));

	resources.pLightIndexCounter = graph.Create("Light Index Counter", BufferDesc::CreateStructured(2, sizeof(uint32), BufferFlag::NoBindless));
	resources.pLightIndexListOpaque = graph.Create("Light List - Opaque", BufferDesc::CreateStructured(MAX_LIGHT_DENSITY, sizeof(uint32)));
	resources.pLightIndexListTransparant = graph.Create("Light List - Transparant", BufferDesc::CreateStructured(MAX_LIGHT_DENSITY, sizeof(uint32)));

	graph.AddPass("2D Light Culling", RGPassFlag::Compute)
		.Read(sceneTextures.pDepth)
		.Write({ resources.pLightGridOpaque, resources.pLightIndexListOpaque })
		.Write({ resources.pLightGridTransparant, resources.pLightIndexListTransparant })
		.Write({ resources.pLightIndexCounter })
		.Bind([=](CommandContext& context)
			{
				Texture* pDepth = sceneTextures.pDepth->Get();

				//#todo: adhoc UAV creation
				context.ClearUAVu(m_pDevice->CreateUAV(resources.pLightIndexCounter->Get(), BufferUAVDesc::CreateRaw()));

				context.SetComputeRootSignature(m_pCommonRS);
				context.SetPipelineState(m_pComputeLightCullPSO);

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
					});

				context.Dispatch(ComputeUtils::GetNumThreadGroups(
					pDepth->GetWidth(), FORWARD_PLUS_BLOCK_SIZE,
					pDepth->GetHeight(), FORWARD_PLUS_BLOCK_SIZE
				));
			});
}

void TiledForward::RenderBasePass(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures, const LightCull2DData& lightCullData, RGTexture* pFogTexture)
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
				context.SetGraphicsRootSignature(m_pCommonRS);

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
						context.SetPipelineState(m_pDiffusePSO);
						Renderer::DrawScene(context, pView, Batch::Blending::Opaque);
					}

					{
						GPU_PROFILE_SCOPE("Opaque Masked", &context);
						context.SetPipelineState(m_pDiffuseMaskedPSO);
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
						context.SetPipelineState(m_pDiffuseAlphaPSO);
						Renderer::DrawScene(context, pView, Batch::Blending::AlphaBlend);
					}
				}
			});
}

void TiledForward::VisualizeLightDensity(RGGraph& graph, GraphicsDevice* pDevice, const SceneView* pView, SceneTextures& sceneTextures, const LightCull2DData& lightCullData)
{
	RGTexture* pVisualizationTarget = graph.Create("Scene Color", sceneTextures.pColorTarget->GetDesc());
	RGTexture* pLightGridOpaque = lightCullData.pLightGridOpaque;

	graph.AddPass("Visualize Light Density", RGPassFlag::Compute)
		.Read({ sceneTextures.pDepth, sceneTextures.pColorTarget, pLightGridOpaque })
		.Write(pVisualizationTarget)
		.Bind([=](CommandContext& context)
			{
				Texture* pTarget = pVisualizationTarget->Get();

				struct
				{
					Vector2i ClusterDimensions;
					Vector2i ClusterSize;
					Vector2 LightGridParams;
				} constantData;

				context.SetComputeRootSignature(m_pCommonRS);
				context.SetPipelineState(m_pVisualizeLightsPSO);

				context.BindRootCBV(0, constantData);
				context.BindRootCBV(1, Renderer::GetViewUniforms(pView, pTarget));
				context.BindResources(3, {
					sceneTextures.pColorTarget->Get()->GetSRV(),
					sceneTextures.pDepth->Get()->GetSRV(),
					pLightGridOpaque->Get()->GetSRV(),
					});
				context.BindResources(2, pTarget->GetUAV());

				context.Dispatch(ComputeUtils::GetNumThreadGroups(
					pTarget->GetWidth(), 16,
					pTarget->GetHeight(), 16));
			});

	sceneTextures.pColorTarget = pVisualizationTarget;
}

