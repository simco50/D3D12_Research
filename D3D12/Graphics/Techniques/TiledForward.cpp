#include "stdafx.h"
#include "TiledForward.h"
#include "Graphics/Core/Shader.h"
#include "Graphics/Core/PipelineState.h"
#include "Graphics/Core/RootSignature.h"
#include "Graphics/Core/Buffer.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/Core/CommandContext.h"
#include "Graphics/Core/Texture.h"
#include "Graphics/Core/ResourceViews.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/Mesh.h"
#include "Graphics/Profiler.h"
#include "Scene/Camera.h"
#include "Graphics/SceneView.h"
#include "Core/ConsoleVariables.h"

static constexpr int MAX_LIGHT_DENSITY = 72000;
static constexpr int FORWARD_PLUS_BLOCK_SIZE = 16;

namespace Tweakables
{
	extern ConsoleVariable<int> g_SsrSamples;
}

TiledForward::TiledForward(GraphicsDevice* pDevice)
	: m_pDevice(pDevice)
{
	SetupPipelines();
}

void TiledForward::OnResize(int windowWidth, int windowHeight)
{
	int frustumCountX = Math::RoundUp((float)windowWidth / FORWARD_PLUS_BLOCK_SIZE);
	int frustumCountY = Math::RoundUp((float)windowHeight / FORWARD_PLUS_BLOCK_SIZE);
	m_pLightGridOpaque = m_pDevice->CreateTexture(TextureDesc::Create2D(frustumCountX, frustumCountY, DXGI_FORMAT_R32G32_UINT, TextureFlag::ShaderResource | TextureFlag::UnorderedAccess), "Light Grid - Opaque");
	m_pLightGridTransparant = m_pDevice->CreateTexture(TextureDesc::Create2D(frustumCountX, frustumCountY, DXGI_FORMAT_R32G32_UINT, TextureFlag::ShaderResource | TextureFlag::UnorderedAccess), "Light Grid - Transparent");
}

void TiledForward::Execute(RGGraph& graph, const SceneView& resources)
{
	RG_GRAPH_SCOPE("Tiled Lighting", graph);

	RGPassBuilder culling = graph.AddPass("Tiled Light Culling");
	culling.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
		{
			context.InsertResourceBarrier(resources.pResolvedDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pLightIndexCounter.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			context.InsertResourceBarrier(m_pLightGridOpaque.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			context.InsertResourceBarrier(m_pLightGridTransparant.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			context.InsertResourceBarrier(m_pLightIndexListBufferOpaque.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			context.InsertResourceBarrier(m_pLightIndexListBufferTransparant.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			context.ClearUavUInt(m_pLightIndexCounter.get(), m_pLightIndexCounterRawUAV);

			context.SetPipelineState(m_pComputeLightCullPSO);
			context.SetComputeRootSignature(m_pComputeLightCullRS.get());

			context.SetRootCBV(0, GetViewUniforms(resources));

			D3D12_CPU_DESCRIPTOR_HANDLE uavs[] = {
				m_pLightIndexCounter->GetUAV()->GetDescriptor(),
				m_pLightIndexListBufferOpaque->GetUAV()->GetDescriptor(),
				m_pLightGridOpaque->GetUAV()->GetDescriptor(),
				m_pLightIndexListBufferTransparant->GetUAV()->GetDescriptor(),
				m_pLightGridTransparant->GetUAV()->GetDescriptor(),
			};
			D3D12_CPU_DESCRIPTOR_HANDLE srvs[] = {
				resources.pResolvedDepth->GetSRV()->GetDescriptor(),
			};

			context.BindResources(1, 0, uavs, ARRAYSIZE(uavs));
			context.BindResources(2, 0, srvs, ARRAYSIZE(srvs));

			context.Dispatch(ComputeUtils::GetNumThreadGroups(
				resources.pResolvedDepth->GetWidth(), FORWARD_PLUS_BLOCK_SIZE,
				resources.pResolvedDepth->GetHeight(), FORWARD_PLUS_BLOCK_SIZE
			));
		});

	//5. BASE PASS
	// - Render the scene using the shadow mapping result and the light culling buffers
	RGPassBuilder basePass = graph.AddPass("Base Pass");
	basePass.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
		{
			context.InsertResourceBarrier(m_pLightGridOpaque.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pLightGridTransparant.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pLightIndexListBufferOpaque.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pLightIndexListBufferTransparant.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(resources.pAO, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(resources.pPreviousColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(resources.pResolvedDepth, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

			context.InsertResourceBarrier(resources.pDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ);
			context.InsertResourceBarrier(resources.pRenderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
			context.InsertResourceBarrier(resources.pNormals, D3D12_RESOURCE_STATE_RENDER_TARGET);

			RenderPassInfo renderPass;
			renderPass.DepthStencilTarget.Access = RenderPassAccess::Load_Store;
			renderPass.DepthStencilTarget.StencilAccess = RenderPassAccess::DontCare_DontCare;
			renderPass.DepthStencilTarget.Target = resources.pDepthBuffer;
			renderPass.DepthStencilTarget.Write = false;
			renderPass.RenderTargetCount = 2;
			renderPass.RenderTargets[0].Access = RenderPassAccess::DontCare_Store;
			renderPass.RenderTargets[0].Target = resources.pRenderTarget;
			renderPass.RenderTargets[1].Access = RenderPassAccess::Clear_Resolve;
			renderPass.RenderTargets[1].Target = resources.pNormals;
			renderPass.RenderTargets[1].ResolveTarget = resources.pResolvedNormals;
			context.BeginRenderPass(renderPass);

			context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context.SetGraphicsRootSignature(m_pDiffuseRS.get());

			context.SetRootCBV(2, GetViewUniforms(resources));

			{
				GPU_PROFILE_SCOPE("Opaque", &context);

				D3D12_CPU_DESCRIPTOR_HANDLE srvs[] = {
					resources.pLightBuffer->GetSRV()->GetDescriptor(), // dummy
					m_pLightGridOpaque->GetSRV()->GetDescriptor(),
					m_pLightIndexListBufferOpaque->GetSRV()->GetDescriptor(),
					resources.pLightBuffer->GetSRV()->GetDescriptor(),
					resources.pAO->GetSRV()->GetDescriptor(),
					resources.pResolvedDepth->GetSRV()->GetDescriptor(),
					resources.pPreviousColor->GetSRV()->GetDescriptor(),
				};
				context.BindResources(3, 0, srvs, ARRAYSIZE(srvs));

				context.SetPipelineState(m_pDiffusePSO);
				DrawScene(context, resources, Batch::Blending::Opaque);

				context.SetPipelineState(m_pDiffuseMaskedPSO);
				DrawScene(context, resources, Batch::Blending::AlphaMask);
			}

			{
				GPU_PROFILE_SCOPE("Transparant", &context);

				D3D12_CPU_DESCRIPTOR_HANDLE srvs[] = {
					resources.pLightBuffer->GetSRV()->GetDescriptor(), // dummy
					m_pLightGridTransparant->GetSRV()->GetDescriptor(),
					m_pLightIndexListBufferTransparant->GetSRV()->GetDescriptor(),
					resources.pLightBuffer->GetSRV()->GetDescriptor(),
					resources.pAO->GetSRV()->GetDescriptor(),
					resources.pResolvedDepth->GetSRV()->GetDescriptor(),
					resources.pPreviousColor->GetSRV()->GetDescriptor(),
				};
				context.BindResources(3, 0, srvs, ARRAYSIZE(srvs));

				context.SetPipelineState(m_pDiffuseAlphaPSO);
				DrawScene(context, resources, Batch::Blending::AlphaBlend);
			}
			context.EndRenderPass();
		});
}

void TiledForward::VisualizeLightDensity(RGGraph& graph, GraphicsDevice* pDevice, const SceneView& resources, Texture* pTarget, Texture* pDepth)
{
	if (!m_pVisualizationIntermediateTexture || m_pVisualizationIntermediateTexture->GetDesc() != pTarget->GetDesc())
	{
		m_pVisualizationIntermediateTexture = m_pDevice->CreateTexture(pTarget->GetDesc(), "LightDensity Debug Texture");
	}

	float sliceMagicA = 0;
	float sliceMagicB = 0;

	RGPassBuilder basePass = graph.AddPass("Visualize Light Density");
	basePass.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
		{
			struct Data
			{
				IntVector2 ClusterDimensions;
				IntVector2 ClusterSize;
				float SliceMagicA;
				float SliceMagicB;
			} constantData{};

			constantData.SliceMagicA = sliceMagicA;
			constantData.SliceMagicB = sliceMagicB;

			context.SetPipelineState(m_pVisualizeLightsPSO);
			context.SetComputeRootSignature(m_pVisualizeLightsRS.get());

			context.InsertResourceBarrier(pTarget, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(pDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pLightGridOpaque.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pVisualizationIntermediateTexture.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			context.SetRootCBV(0, constantData);

			context.SetRootCBV(1, GetViewUniforms(resources));

			context.BindResource(2, 0, pTarget->GetSRV());
			context.BindResource(2, 1, pDepth->GetSRV());
			context.BindResource(2, 2, m_pLightGridOpaque->GetSRV());

			context.BindResource(3, 0, m_pVisualizationIntermediateTexture->GetUAV());

			context.Dispatch(ComputeUtils::GetNumThreadGroups(
				pTarget->GetWidth(), 16,
				pTarget->GetHeight(), 16));

			context.InsertUavBarrier();

			context.CopyTexture(m_pVisualizationIntermediateTexture.get(), pTarget);
		});
}

void TiledForward::SetupPipelines()
{
	// Light culling
	{
		Shader* pComputeShader = m_pDevice->GetShader("LightCulling.hlsl", ShaderType::Compute, "CSMain");

		m_pComputeLightCullRS = std::make_unique<RootSignature>(m_pDevice);
		m_pComputeLightCullRS->FinalizeFromShader("Tiled Light Culling", pComputeShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetRootSignature(m_pComputeLightCullRS->GetRootSignature());
		psoDesc.SetName("Tiled Light Culling");
		m_pComputeLightCullPSO = m_pDevice->CreatePipeline(psoDesc);

		m_pLightIndexCounter = m_pDevice->CreateBuffer(BufferDesc::CreateStructured(2, sizeof(uint32)), "Light Index Counter");
		m_pLightIndexCounter->CreateUAV(&m_pLightIndexCounterRawUAV, BufferUAVDesc::CreateRaw());
		m_pLightIndexListBufferOpaque = m_pDevice->CreateBuffer(BufferDesc::CreateStructured(MAX_LIGHT_DENSITY, sizeof(uint32)), "Light List Opaque");
		m_pLightIndexListBufferTransparant = m_pDevice->CreateBuffer(BufferDesc::CreateStructured(MAX_LIGHT_DENSITY, sizeof(uint32)), "Light List Transparant");
	}

	// Shading pipelines
	{
		Shader* pVertexShader = m_pDevice->GetShader("Diffuse.hlsl", ShaderType::Vertex, "VSMain", { "TILED_FORWARD" });
		Shader* pPixelShader = m_pDevice->GetShader("Diffuse.hlsl", ShaderType::Pixel, "PSMain", { "TILED_FORWARD" });

		m_pDiffuseRS = std::make_unique<RootSignature>(m_pDevice);
		m_pDiffuseRS->FinalizeFromShader("Diffuse", pVertexShader);

		{
			DXGI_FORMAT formats[] = {
				DXGI_FORMAT_R16G16B16A16_FLOAT,
				DXGI_FORMAT_R16G16B16A16_FLOAT,
			};

			//Opaque
			PipelineStateInitializer psoDesc;
			psoDesc.SetRootSignature(m_pDiffuseRS->GetRootSignature());
			psoDesc.SetVertexShader(pVertexShader);
			psoDesc.SetPixelShader(pPixelShader);
			psoDesc.SetRenderTargetFormats(formats, ARRAYSIZE(formats), DXGI_FORMAT_D32_FLOAT, /* m_pDevice->GetMultiSampleCount() */ 1);
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
		Shader* pComputeShader = m_pDevice->GetShader("VisualizeLightCount.hlsl", ShaderType::Compute, "DebugLightDensityCS", { "TILED_FORWARD" });

		m_pVisualizeLightsRS = std::make_unique<RootSignature>(m_pDevice);
		m_pVisualizeLightsRS->FinalizeFromShader("Light Density Visualization", pComputeShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetRootSignature(m_pVisualizeLightsRS->GetRootSignature());
		psoDesc.SetName("Light Density Visualization");
		m_pVisualizeLightsPSO = m_pDevice->CreatePipeline(psoDesc);
	}
}
