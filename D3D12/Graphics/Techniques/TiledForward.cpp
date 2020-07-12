#include "stdafx.h"
#include "TiledForward.h"
#include "Graphics/Core/Shader.h"
#include "Graphics/Core/PipelineState.h"
#include "Graphics/Core/RootSignature.h"
#include "Graphics/Core/GraphicsBuffer.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/Core/CommandContext.h"
#include "Graphics/Core/Texture.h"
#include "Graphics/Core/ResourceViews.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/Mesh.h"
#include "Graphics/Profiler.h"
#include "Scene/Camera.h"

static constexpr int MAX_LIGHT_DENSITY = 72000;
static constexpr int FORWARD_PLUS_BLOCK_SIZE = 16;

bool g_VisualizeLightDensity = false;

TiledForward::TiledForward(Graphics* pGraphics)
{
	SetupResources(pGraphics);
	SetupPipelines(pGraphics);
}

void TiledForward::OnSwapchainCreated(int windowWidth, int windowHeight)
{
	int frustumCountX = Math::RoundUp((float)windowWidth / FORWARD_PLUS_BLOCK_SIZE);
	int frustumCountY = Math::RoundUp((float)windowHeight / FORWARD_PLUS_BLOCK_SIZE);
	m_pLightGridOpaque->Create(TextureDesc::Create2D(frustumCountX, frustumCountY, DXGI_FORMAT_R32G32_UINT, TextureFlag::ShaderResource | TextureFlag::UnorderedAccess));
	m_pLightGridTransparant->Create(TextureDesc::Create2D(frustumCountX, frustumCountY, DXGI_FORMAT_R32G32_UINT, TextureFlag::ShaderResource | TextureFlag::UnorderedAccess));
}

void TiledForward::Execute(RGGraph& graph, const TiledForwardInputResources& resources)
{
	RG_GRAPH_SCOPE("Tiled Lighting", graph);

	RGPassBuilder culling = graph.AddPass("Light Culling");
	culling.Read(resources.ResolvedDepthBuffer);
	culling.Bind([=](CommandContext& context, const RGPassResources& passResources)
		{
			Texture* pDepthTexture = passResources.GetTexture(resources.ResolvedDepthBuffer);
			context.InsertResourceBarrier(pDepthTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pLightIndexCounter.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			context.InsertResourceBarrier(m_pLightGridOpaque.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			context.InsertResourceBarrier(m_pLightGridTransparant.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			context.InsertResourceBarrier(m_pLightIndexListBufferOpaque.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			context.InsertResourceBarrier(m_pLightIndexListBufferTransparant.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			context.ClearUavUInt(m_pLightIndexCounter.get(), m_pLightIndexCounterRawUAV);

			context.SetPipelineState(m_pComputeLightCullPSO.get());
			context.SetComputeRootSignature(m_pComputeLightCullRS.get());

			struct ShaderParameters
			{
				Matrix CameraView;
				Matrix ProjectionInverse;
				IntVector3 NumThreadGroups;
				int padding0;
				Vector2 ScreenDimensionsInv;
				uint32 LightCount;
			} Data{};

			Data.CameraView = resources.pCamera->GetView();
			Data.NumThreadGroups.x = Math::DivideAndRoundUp(pDepthTexture->GetWidth(), FORWARD_PLUS_BLOCK_SIZE);
			Data.NumThreadGroups.y = Math::DivideAndRoundUp(pDepthTexture->GetHeight(), FORWARD_PLUS_BLOCK_SIZE);
			Data.NumThreadGroups.z = 1;
			Data.ScreenDimensionsInv = Vector2(1.0f / pDepthTexture->GetWidth(), 1.0f / pDepthTexture->GetHeight());
			Data.LightCount = (uint32)resources.pLightBuffer->GetDesc().ElementCount;
			Data.ProjectionInverse = resources.pCamera->GetProjectionInverse();

			context.SetComputeDynamicConstantBufferView(0, &Data, sizeof(ShaderParameters));
			context.SetDynamicDescriptor(1, 0, m_pLightIndexCounter->GetUAV());
			context.SetDynamicDescriptor(1, 1, m_pLightIndexListBufferOpaque->GetUAV());
			context.SetDynamicDescriptor(1, 2, m_pLightGridOpaque->GetUAV());
			context.SetDynamicDescriptor(1, 3, m_pLightIndexListBufferTransparant->GetUAV());
			context.SetDynamicDescriptor(1, 4, m_pLightGridTransparant->GetUAV());
			context.SetDynamicDescriptor(2, 0, pDepthTexture->GetSRV());
			context.SetDynamicDescriptor(2, 1, resources.pLightBuffer->GetSRV());

			context.Dispatch(Data.NumThreadGroups);
		});

	//5. BASE PASS
	// - Render the scene using the shadow mapping result and the light culling buffers
	RGPassBuilder basePass = graph.AddPass("Base Pass");
	basePass.Read(resources.DepthBuffer);
	basePass.Bind([=](CommandContext& context, const RGPassResources& passResources)
		{
			Texture* pDepthTexture = passResources.GetTexture(resources.DepthBuffer);

			struct PerFrameData
			{
				Matrix ViewInverse;
				Matrix View;
			} frameData;

			struct PerObjectData
			{
				Matrix World;
				Matrix WorldViewProjection;
			} ObjectData{};

			//Camera constants
			frameData.ViewInverse = resources.pCamera->GetViewInverse();
			frameData.View = resources.pCamera->GetView();

			context.SetViewport(FloatRect(0, 0, (float)pDepthTexture->GetWidth(), (float)pDepthTexture->GetHeight()));

			context.InsertResourceBarrier(m_pLightGridOpaque.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pLightGridTransparant.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pLightIndexListBufferOpaque.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pLightIndexListBufferTransparant.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(resources.pShadowMap, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(resources.pRenderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
			context.InsertResourceBarrier(pDepthTexture, D3D12_RESOURCE_STATE_DEPTH_READ);

			context.BeginRenderPass(RenderPassInfo(resources.pRenderTarget, RenderPassAccess::Clear_Store, pDepthTexture, RenderPassAccess::Load_DontCare));

			context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context.SetGraphicsRootSignature(m_pDiffuseRS.get());

			context.SetDynamicConstantBufferView(1, &frameData, sizeof(PerFrameData));
			context.SetDynamicConstantBufferView(2, resources.pShadowData, sizeof(ShadowData));

			context.SetDynamicDescriptor(4, 0, resources.pShadowMap->GetSRV());
			context.SetDynamicDescriptor(4, 3, resources.pLightBuffer->GetSRV());
			{
				GPU_PROFILE_SCOPE("Opaque", &context);
				context.SetPipelineState(g_VisualizeLightDensity ? m_pVisualizeDensityPSO.get() : m_pDiffusePSO.get());

				context.SetDynamicDescriptor(4, 1, m_pLightGridOpaque->GetSRV());
				context.SetDynamicDescriptor(4, 2, m_pLightIndexListBufferOpaque->GetSRV()->GetDescriptor());

				for (const Batch& b : *resources.pOpaqueBatches)
				{
					ObjectData.World = b.WorldMatrix;
					ObjectData.WorldViewProjection = ObjectData.World * resources.pCamera->GetViewProjection();
					context.SetDynamicConstantBufferView(0, &ObjectData, sizeof(PerObjectData));
					context.SetDynamicDescriptor(3, 0, b.pMaterial->pDiffuseTexture->GetSRV());
					context.SetDynamicDescriptor(3, 1, b.pMaterial->pNormalTexture->GetSRV());
					context.SetDynamicDescriptor(3, 2, b.pMaterial->pSpecularTexture->GetSRV());
					b.pMesh->Draw(&context);
				}
			}

			{
				GPU_PROFILE_SCOPE("Transparant", &context);
				context.SetPipelineState(g_VisualizeLightDensity ? m_pVisualizeDensityPSO.get() : m_pDiffuseAlphaPSO.get());

				context.SetDynamicDescriptor(4, 1, m_pLightGridTransparant->GetSRV());
				context.SetDynamicDescriptor(4, 2, m_pLightIndexListBufferTransparant->GetSRV()->GetDescriptor());

				for (const Batch& b : *resources.pTransparantBatches)
				{
					ObjectData.World = b.WorldMatrix;
					ObjectData.WorldViewProjection = ObjectData.World * resources.pCamera->GetViewProjection();
					context.SetDynamicConstantBufferView(0, &ObjectData, sizeof(PerObjectData));
					context.SetDynamicDescriptor(3, 0, b.pMaterial->pDiffuseTexture->GetSRV());
					context.SetDynamicDescriptor(3, 1, b.pMaterial->pNormalTexture->GetSRV());
					context.SetDynamicDescriptor(3, 2, b.pMaterial->pSpecularTexture->GetSRV());
					b.pMesh->Draw(&context);
				}
			}
			context.EndRenderPass();
		});
}

void TiledForward::SetupResources(Graphics* pGraphics)
{
	m_pLightGridOpaque = std::make_unique<Texture>(pGraphics, "Opaque Light Grid");
	m_pLightGridTransparant = std::make_unique<Texture>(pGraphics, "Transparant Light Grid");
}

void TiledForward::SetupPipelines(Graphics* pGraphics)
{
	Shader computeShader("LightCulling.hlsl", ShaderType::Compute, "CSMain");

	m_pComputeLightCullRS = std::make_unique<RootSignature>();
	m_pComputeLightCullRS->FinalizeFromShader("Tiled Light Culling RS", computeShader, pGraphics->GetDevice());

	m_pComputeLightCullPSO = std::make_unique<PipelineState>();
	m_pComputeLightCullPSO->SetComputeShader(computeShader);
	m_pComputeLightCullPSO->SetRootSignature(m_pComputeLightCullRS->GetRootSignature());
	m_pComputeLightCullPSO->Finalize("Tiled Light Culling PSO", pGraphics->GetDevice());

	m_pLightIndexCounter = std::make_unique<Buffer>(pGraphics, "Light Index Counter");
	m_pLightIndexCounter->Create(BufferDesc::CreateStructured(2, sizeof(uint32)));
	m_pLightIndexCounter->CreateUAV(&m_pLightIndexCounterRawUAV, BufferUAVDesc::CreateRaw());
	m_pLightIndexListBufferOpaque = std::make_unique<Buffer>(pGraphics, "Light List Opaque");
	m_pLightIndexListBufferOpaque->Create(BufferDesc::CreateStructured(MAX_LIGHT_DENSITY, sizeof(uint32)));
	m_pLightIndexListBufferTransparant = std::make_unique<Buffer>(pGraphics, "Light List Transparant");
	m_pLightIndexListBufferTransparant->Create(BufferDesc::CreateStructured(MAX_LIGHT_DENSITY, sizeof(uint32)));

	CD3DX12_INPUT_ELEMENT_DESC inputElements[] = {
		CD3DX12_INPUT_ELEMENT_DESC("POSITION", DXGI_FORMAT_R32G32B32_FLOAT),
		CD3DX12_INPUT_ELEMENT_DESC("TEXCOORD", DXGI_FORMAT_R32G32_FLOAT),
		CD3DX12_INPUT_ELEMENT_DESC("NORMAL", DXGI_FORMAT_R32G32B32_FLOAT),
		CD3DX12_INPUT_ELEMENT_DESC("TANGENT", DXGI_FORMAT_R32G32B32_FLOAT),
		CD3DX12_INPUT_ELEMENT_DESC("TEXCOORD", DXGI_FORMAT_R32G32B32_FLOAT, 1),
	};

	//PBR Diffuse passes
	{
		//Shaders
		Shader vertexShader("Diffuse.hlsl", ShaderType::Vertex, "VSMain", { });
		Shader pixelShader("Diffuse.hlsl", ShaderType::Pixel, "PSMain", { });
		Shader debugPixelShader("Diffuse.hlsl", ShaderType::Pixel, "DebugLightDensityPS", { });

		//Rootsignature
		m_pDiffuseRS = std::make_unique<RootSignature>();
		m_pDiffuseRS->FinalizeFromShader("Diffuse", vertexShader, pGraphics->GetDevice());

		{
			//Opaque
			m_pDiffusePSO = std::make_unique<PipelineState>();
			m_pDiffusePSO->SetInputLayout(inputElements, sizeof(inputElements) / sizeof(inputElements[0]));
			m_pDiffusePSO->SetRootSignature(m_pDiffuseRS->GetRootSignature());
			m_pDiffusePSO->SetVertexShader(vertexShader);
			m_pDiffusePSO->SetPixelShader(pixelShader);
			m_pDiffusePSO->SetRenderTargetFormat(Graphics::RENDER_TARGET_FORMAT, Graphics::DEPTH_STENCIL_FORMAT, pGraphics->GetMultiSampleCount());
			m_pDiffusePSO->SetDepthTest(D3D12_COMPARISON_FUNC_EQUAL);
			m_pDiffusePSO->SetDepthWrite(false);
			m_pDiffusePSO->Finalize("Diffuse PBR Pipeline", pGraphics->GetDevice());

			//Transparant
			m_pDiffuseAlphaPSO = std::make_unique<PipelineState>(*m_pDiffusePSO.get());
			m_pDiffuseAlphaPSO->SetBlendMode(BlendMode::Alpha, false);
			m_pDiffuseAlphaPSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
			m_pDiffuseAlphaPSO->Finalize("Diffuse PBR (Alpha) Pipeline", pGraphics->GetDevice());

			//Debug Density
			m_pVisualizeDensityPSO = std::make_unique<PipelineState>(*m_pDiffusePSO.get());
			m_pVisualizeDensityPSO->SetPixelShader(debugPixelShader);
			m_pVisualizeDensityPSO->Finalize("Debug Light Density Pipeline", pGraphics->GetDevice());
		}
	}
}