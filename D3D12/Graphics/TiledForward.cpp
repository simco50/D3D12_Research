#include "stdafx.h"
#include "TiledForward.h"
#include "Graphics/Shader.h"
#include "Graphics/PipelineState.h"
#include "Graphics/RootSignature.h"
#include "Graphics/GraphicsBuffer.h"
#include "Graphics/Graphics.h"
#include "Graphics/CommandContext.h"
#include "Graphics/CommandQueue.h"
#include "Graphics/Texture.h"
#include "Graphics/Mesh.h"
#include "Graphics/Light.h"
#include "Graphics/Profiler.h"
#include "Scene/Camera.h"
#include "ResourceViews.h"
#include "RenderGraph/RenderGraph.h"

static constexpr int MAX_LIGHT_DENSITY = 72000;
static constexpr int FORWARD_PLUS_BLOCK_SIZE = 16;

TiledForward::TiledForward(Graphics* pGraphics)
	: m_pGraphics(pGraphics)
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
	graph.AddPass("Light Culling", [&](RGPassBuilder& builder)
		{
			builder.NeverCull();
			builder.Read(resources.ResolvedDepthBuffer);
			return [=](CommandContext& context, const RGPassResources& passResources)
			{
				Texture* pDepthTexture = passResources.GetTexture(resources.ResolvedDepthBuffer);
				context.InsertResourceBarrier(pDepthTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				context.InsertResourceBarrier(m_pLightIndexCounter.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

				context.InsertResourceBarrier(m_pLightGridOpaque.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				context.InsertResourceBarrier(m_pLightGridTransparant.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				context.InsertResourceBarrier(m_pLightIndexListBufferOpaque.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				context.InsertResourceBarrier(m_pLightIndexListBufferTransparant.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

				context.ClearUavUInt(m_pLightIndexCounter.get(), m_pLightIndexCounterRawUAV);

				context.SetComputePipelineState(m_pComputeLightCullPSO.get());
				context.SetComputeRootSignature(m_pComputeLightCullRS.get());

				struct ShaderParameters
				{
					Matrix CameraView;
					Matrix ProjectionInverse;
					uint32 NumThreadGroups[4];
					Vector2 ScreenDimensions;
					uint32 LightCount;
				} Data{};

				Data.CameraView = resources.pCamera->GetView();
				Data.NumThreadGroups[0] = Math::DivideAndRoundUp(pDepthTexture->GetWidth(), FORWARD_PLUS_BLOCK_SIZE);
				Data.NumThreadGroups[1] = Math::DivideAndRoundUp(pDepthTexture->GetHeight(), FORWARD_PLUS_BLOCK_SIZE);
				Data.NumThreadGroups[2] = 1;
				Data.ScreenDimensions.x = (float)pDepthTexture->GetWidth();
				Data.ScreenDimensions.y = (float)pDepthTexture->GetHeight();
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

				context.Dispatch(Data.NumThreadGroups[0], Data.NumThreadGroups[1], Data.NumThreadGroups[2]);
			};
		});

	//5. BASE PASS
	// - Render the scene using the shadow mapping result and the light culling buffers
	graph.AddPass("Base Pass", [&](RGPassBuilder& builder)
		{
			builder.NeverCull();
			builder.Read(resources.DepthBuffer);
			return [=](CommandContext& context, const RGPassResources& passResources)
			{
				Texture* pDepth = passResources.GetTexture(resources.DepthBuffer);

				struct PerFrameData
				{
					Matrix ViewInverse;
				} frameData;

				struct PerObjectData
				{
					Matrix World;
					Matrix WorldViewProjection;
				} ObjectData{};

				constexpr const int MAX_SHADOW_CASTERS = 8;
				struct LightData
				{
					Matrix LightViewProjections[MAX_SHADOW_CASTERS];
					Vector4 ShadowMapOffsets[MAX_SHADOW_CASTERS];
				} lightData;

				//Camera constants
				frameData.ViewInverse = resources.pCamera->GetViewInverse();

				context.SetViewport(FloatRect(0, 0, (float)pDepth->GetWidth(), (float)pDepth->GetHeight()));

				context.InsertResourceBarrier(m_pLightGridOpaque.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
				context.InsertResourceBarrier(m_pLightGridTransparant.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
				context.InsertResourceBarrier(m_pLightIndexListBufferOpaque.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
				context.InsertResourceBarrier(m_pLightIndexListBufferTransparant.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
				context.InsertResourceBarrier(resources.pShadowMap, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
				context.InsertResourceBarrier(resources.pRenderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);

				context.BeginRenderPass(RenderPassInfo(resources.pRenderTarget, RenderPassAccess::Clear_Store, pDepth, RenderPassAccess::Load_DontCare));

				context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				context.SetGraphicsRootSignature(m_pDiffuseRS.get());

				context.SetDynamicConstantBufferView(1, &frameData, sizeof(PerFrameData));
				context.SetDynamicConstantBufferView(2, &lightData, sizeof(LightData));

				context.SetDynamicDescriptor(4, 0, resources.pShadowMap->GetSRV());
				context.SetDynamicDescriptor(4, 3, resources.pLightBuffer->GetSRV());
				{
					GPU_PROFILE_SCOPE("Opaque", &context);
					context.SetGraphicsPipelineState(m_pDiffusePSO.get());

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
					context.SetGraphicsPipelineState(m_pDiffuseAlphaPSO.get());

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
			};
		});
}

void TiledForward::SetupResources(Graphics* pGraphics)
{
	m_pLightGridOpaque = std::make_unique<Texture>(pGraphics, "Opaque Light Grid");
	m_pLightGridTransparant = std::make_unique<Texture>(pGraphics, "Transparant Light Grid");
}

void TiledForward::SetupPipelines(Graphics* pGraphics)
{
	Shader computeShader("Resources/Shaders/LightCulling.hlsl", Shader::Type::ComputeShader, "CSMain");

	m_pComputeLightCullRS = std::make_unique<RootSignature>();
	m_pComputeLightCullRS->FinalizeFromShader("Tiled Light Culling RS", computeShader, pGraphics->GetDevice());

	m_pComputeLightCullPSO = std::make_unique<ComputePipelineState>();
	m_pComputeLightCullPSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
	m_pComputeLightCullPSO->SetRootSignature(m_pComputeLightCullRS->GetRootSignature());
	m_pComputeLightCullPSO->Finalize("Tiled Light Culling PSO", pGraphics->GetDevice());

	m_pLightIndexCounter = std::make_unique<Buffer>(pGraphics, "Light Index Counter");
	m_pLightIndexCounter->Create(BufferDesc::CreateStructured(2, sizeof(uint32)));
	m_pLightIndexCounter->CreateUAV(&m_pLightIndexCounterRawUAV, BufferUAVDesc::CreateRaw());
	m_pLightIndexListBufferOpaque = std::make_unique<Buffer>(pGraphics, "Light List Opaque");
	m_pLightIndexListBufferOpaque->Create(BufferDesc::CreateStructured(MAX_LIGHT_DENSITY, sizeof(uint32)));
	m_pLightIndexListBufferTransparant = std::make_unique<Buffer>(pGraphics, "Light List Transparant");
	m_pLightIndexListBufferTransparant->Create(BufferDesc::CreateStructured(MAX_LIGHT_DENSITY, sizeof(uint32)));

	D3D12_INPUT_ELEMENT_DESC inputElements[] = {
	D3D12_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	D3D12_INPUT_ELEMENT_DESC{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	D3D12_INPUT_ELEMENT_DESC{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 1, DXGI_FORMAT_R32G32B32_FLOAT, 0, 44, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	//PBR Diffuse passes
	{
		//Shaders
		Shader vertexShader("Resources/Shaders/Diffuse.hlsl", Shader::Type::VertexShader, "VSMain", { /*"SHADOW"*/ });
		Shader pixelShader("Resources/Shaders/Diffuse.hlsl", Shader::Type::PixelShader, "PSMain", { /*"SHADOW"*/ });

		//Rootsignature
		m_pDiffuseRS = std::make_unique<RootSignature>();
		m_pDiffuseRS->FinalizeFromShader("Diffuse", vertexShader, pGraphics->GetDevice());

		{
			//Opaque
			m_pDiffusePSO = std::make_unique<GraphicsPipelineState>();
			m_pDiffusePSO->SetInputLayout(inputElements, sizeof(inputElements) / sizeof(inputElements[0]));
			m_pDiffusePSO->SetRootSignature(m_pDiffuseRS->GetRootSignature());
			m_pDiffusePSO->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
			m_pDiffusePSO->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
			m_pDiffusePSO->SetRenderTargetFormat(Graphics::RENDER_TARGET_FORMAT, Graphics::DEPTH_STENCIL_FORMAT, pGraphics->GetMultiSampleCount(), pGraphics->GetMultiSampleQualityLevel(pGraphics->GetMultiSampleCount()));
			m_pDiffusePSO->SetDepthTest(D3D12_COMPARISON_FUNC_EQUAL);
			m_pDiffusePSO->SetDepthWrite(false);
			m_pDiffusePSO->Finalize("Diffuse PBR Pipeline", pGraphics->GetDevice());

			//Transparant
			m_pDiffuseAlphaPSO = std::make_unique<GraphicsPipelineState>(*m_pDiffusePSO.get());
			m_pDiffuseAlphaPSO->SetBlendMode(BlendMode::Alpha, false);
			m_pDiffuseAlphaPSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
			m_pDiffuseAlphaPSO->Finalize("Diffuse PBR (Alpha) Pipeline", pGraphics->GetDevice());
		}
	}
}