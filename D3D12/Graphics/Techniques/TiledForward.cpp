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

struct CullBlackboardData
{
	RGTexture* pLightGridOpaque;
};
RG_BLACKBOARD_DATA(CullBlackboardData);

TiledForward::TiledForward(GraphicsDevice* pDevice)
	: m_pDevice(pDevice)
{
	// Light culling
	{
		m_pComputeLightCullRS = new RootSignature(m_pDevice);
		m_pComputeLightCullRS->AddConstantBufferView(100);
		m_pComputeLightCullRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 5);
		m_pComputeLightCullRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2);
		m_pComputeLightCullRS->Finalize("Tiled Light Culling");
		m_pComputeLightCullPSO = m_pDevice->CreateComputePipeline(m_pComputeLightCullRS, "LightCulling.hlsl", "CSMain");
	}

	// Shading pipelines
	{
		m_pDiffuseRS = new RootSignature(m_pDevice);
		m_pDiffuseRS->AddRootConstants(0, 3);
		m_pDiffuseRS->AddConstantBufferView(1);
		m_pDiffuseRS->AddConstantBufferView(100);
		m_pDiffuseRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 8);
		m_pDiffuseRS->Finalize("Diffuse");

		{
			constexpr DXGI_FORMAT formats[] = {
				DXGI_FORMAT_R16G16B16A16_FLOAT,
				DXGI_FORMAT_R16G16_FLOAT,
				DXGI_FORMAT_R8_UNORM,
			};

			//Opaque
			PipelineStateInitializer psoDesc;
			psoDesc.SetRootSignature(m_pDiffuseRS);
			psoDesc.SetVertexShader("Diffuse.hlsl", "VSMain", { "TILED_FORWARD" });
			psoDesc.SetPixelShader("Diffuse.hlsl", "PSMain", { "TILED_FORWARD" });
			psoDesc.SetRenderTargetFormats(formats, DXGI_FORMAT_D32_FLOAT, 1);
			psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_EQUAL);
			psoDesc.SetDepthWrite(false);
			psoDesc.SetName("Diffuse");
			m_pDiffusePSO = m_pDevice->CreatePipeline(psoDesc);

			//Alpha Mask
			psoDesc.SetCullMode(D3D12_CULL_MODE_NONE);
			psoDesc.SetName("Diffuse Masked");
			m_pDiffuseMaskedPSO = m_pDevice->CreatePipeline(psoDesc);

			//Transparant
			psoDesc.SetBlendMode(BlendMode::Alpha, false);
			psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
			psoDesc.SetName("Diffuse (Alpha)");
			m_pDiffuseAlphaPSO = m_pDevice->CreatePipeline(psoDesc);
		}
	}

	// Light count visualization
	{
		m_pVisualizeLightsRS = new RootSignature(m_pDevice);
		m_pVisualizeLightsRS->AddConstantBufferView(0);
		m_pVisualizeLightsRS->AddConstantBufferView(100);
		m_pVisualizeLightsRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3);
		m_pVisualizeLightsRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 3);
		m_pVisualizeLightsRS->Finalize("Light Density Visualization");

		m_pVisualizeLightsPSO = m_pDevice->CreateComputePipeline(m_pVisualizeLightsRS, "VisualizeLightCount.hlsl", "DebugLightDensityCS", { "TILED_FORWARD" });
	}
}

void TiledForward::Execute(RGGraph& graph, const SceneView& view, SceneTextures& sceneTextures)
{
	int frustumCountX = Math::DivideAndRoundUp(view.GetDimensions().x, FORWARD_PLUS_BLOCK_SIZE);
	int frustumCountY = Math::DivideAndRoundUp(view.GetDimensions().y, FORWARD_PLUS_BLOCK_SIZE);
	RGTexture* pLightGridOpaque = graph.CreateTexture("Light Grid - Opaque", TextureDesc::Create2D(frustumCountX, frustumCountY, DXGI_FORMAT_R32G32_UINT));
	RGTexture* pLightGridTransparant = graph.CreateTexture("Light Grid - Transparant", TextureDesc::Create2D(frustumCountX, frustumCountY, DXGI_FORMAT_R32G32_UINT));

	RGBuffer* pLightIndexCounter = graph.CreateBuffer("Light Index Counter", BufferDesc::CreateStructured(2, sizeof(uint32), BufferFlag::NoBindless));
	RGBuffer* pLightIndexListOpaque = graph.CreateBuffer("Light List - Opaque", BufferDesc::CreateStructured(MAX_LIGHT_DENSITY, sizeof(uint32)));
	RGBuffer* pLightIndexListTransparant = graph.CreateBuffer("Light List - Transparant", BufferDesc::CreateStructured(MAX_LIGHT_DENSITY, sizeof(uint32)));

	graph.AddPass("Tiled Light Culling", RGPassFlag::Compute)
		.Read(sceneTextures.pDepth)
		.Write({ pLightGridOpaque, pLightGridTransparant, pLightIndexListOpaque, pLightIndexListTransparant})
		.ReadWrite(pLightIndexCounter)
		.Bind([=](CommandContext& context, const RGPassResources& resources)
			{
				Texture* pDepth = sceneTextures.pDepth->Get();

				//#todo: adhoc UAV creation
				context.ClearUavUInt(pLightIndexCounter->Get(), m_pDevice->CreateUAV(pLightIndexCounter->Get(), BufferUAVDesc::CreateRaw()));

				context.SetPipelineState(m_pComputeLightCullPSO);
				context.SetComputeRootSignature(m_pComputeLightCullRS);

				context.SetRootCBV(0, Renderer::GetViewUniforms(view, pDepth));

				context.BindResources(1, {
					pLightIndexCounter->Get()->GetUAV(),
					pLightIndexListOpaque->Get()->GetUAV(),
					pLightGridOpaque->Get()->GetUAV(),
					pLightIndexListTransparant->Get()->GetUAV(),
					pLightGridTransparant->Get()->GetUAV(),
					});
				context.BindResources(2, {
					pDepth->GetSRV(),
					});

				context.Dispatch(ComputeUtils::GetNumThreadGroups(
					pDepth->GetWidth(), FORWARD_PLUS_BLOCK_SIZE,
					pDepth->GetHeight(), FORWARD_PLUS_BLOCK_SIZE
				));
			});

	//5. BASE PASS
	// - Render the scene using the shadow mapping result and the light culling buffers
	graph.AddPass("Base Pass", RGPassFlag::Raster)
		.Read({ sceneTextures.pAmbientOcclusion, sceneTextures.pPreviousColor })
		.Read({ pLightGridOpaque, pLightGridTransparant, pLightIndexListOpaque, pLightIndexListTransparant })
		.DepthStencil(sceneTextures.pDepth, RenderPassAccess::Load_Store, false)
		.RenderTarget(sceneTextures.pColorTarget, RenderPassAccess::DontCare_Store)
		.RenderTarget(sceneTextures.pNormals, RenderPassAccess::DontCare_Store)
		.RenderTarget(sceneTextures.pRoughness, RenderPassAccess::DontCare_Store)
		.Bind([=](CommandContext& context, const RGPassResources& resources)
			{
				Texture* pTarget = sceneTextures.pColorTarget->Get();

				context.BeginRenderPass(resources.GetRenderPassInfo());

				context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				context.SetGraphicsRootSignature(m_pDiffuseRS);

				context.SetRootCBV(2, Renderer::GetViewUniforms(view, pTarget));

				{
					GPU_PROFILE_SCOPE("Opaque", &context);

					context.BindResources(3, {
						sceneTextures.pAmbientOcclusion->Get()->GetSRV(),
						sceneTextures.pDepth->Get()->GetSRV(),
						sceneTextures.pPreviousColor->Get()->GetSRV(),
						GraphicsCommon::GetDefaultTexture(DefaultTexture::Black3D)->GetSRV(),
						pLightGridOpaque->Get()->GetSRV(),
						pLightIndexListOpaque->Get()->GetSRV(),
						});

					context.SetPipelineState(m_pDiffusePSO);
					Renderer::DrawScene(context, view, Batch::Blending::Opaque);

					context.SetPipelineState(m_pDiffuseMaskedPSO);
					Renderer::DrawScene(context, view, Batch::Blending::AlphaMask);
				}

				{
					GPU_PROFILE_SCOPE("Transparant", &context);

					context.BindResources(3, {
						sceneTextures.pAmbientOcclusion->Get()->GetSRV(),
						sceneTextures.pDepth->Get()->GetSRV(),
						sceneTextures.pPreviousColor->Get()->GetSRV(),
						GraphicsCommon::GetDefaultTexture(DefaultTexture::Black3D)->GetSRV(),
						pLightGridTransparant->Get()->GetSRV(),
						pLightIndexListTransparant->Get()->GetSRV(),
						});

					context.SetPipelineState(m_pDiffuseAlphaPSO);
					Renderer::DrawScene(context, view, Batch::Blending::AlphaBlend);
				}
				context.EndRenderPass();
			});

	CullBlackboardData& blackboardData = graph.Blackboard.Add<CullBlackboardData>();
	blackboardData.pLightGridOpaque = pLightGridOpaque;
}

void TiledForward::VisualizeLightDensity(RGGraph& graph, GraphicsDevice* pDevice, const SceneView& view, SceneTextures& sceneTextures)
{
	RGTexture* pVisualizationIntermediate = graph.CreateTexture("Light Density Debug Texture", sceneTextures.pColorTarget->GetDesc());

	const CullBlackboardData& blackboardData = graph.Blackboard.Get<CullBlackboardData>();
	RGTexture* pLightGridOpaque = blackboardData.pLightGridOpaque;

	graph.AddCopyPass("Cache Scene Color", sceneTextures.pColorTarget, pVisualizationIntermediate);

	graph.AddPass("Visualize Light Density", RGPassFlag::Raster)
		.Read({ sceneTextures.pDepth, pVisualizationIntermediate, pLightGridOpaque })
		.Write(sceneTextures.pColorTarget)
		.Bind([=](CommandContext& context, const RGPassResources& resources)
			{
				Texture* pTarget = sceneTextures.pColorTarget->Get();

				struct
				{
					IntVector2 ClusterDimensions;
					IntVector2 ClusterSize;
					Vector2 LightGridParams;
				} constantData;

				context.SetPipelineState(m_pVisualizeLightsPSO);
				context.SetComputeRootSignature(m_pVisualizeLightsRS);
				context.SetRootCBV(0, constantData);
				context.SetRootCBV(1, Renderer::GetViewUniforms(view, pTarget));

				context.BindResources(2, {
					pVisualizationIntermediate->Get()->GetSRV(),
					sceneTextures.pDepth->Get()->GetSRV(),
					pLightGridOpaque->Get()->GetSRV(),
					});
				context.BindResources(3, pTarget->GetUAV());

				context.Dispatch(ComputeUtils::GetNumThreadGroups(
					pTarget->GetWidth(), 16,
					pTarget->GetHeight(), 16));
			});
}

