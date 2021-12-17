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
			Data.NumThreadGroups.x = Math::DivideAndRoundUp(resources.pResolvedDepth->GetWidth(), FORWARD_PLUS_BLOCK_SIZE);
			Data.NumThreadGroups.y = Math::DivideAndRoundUp(resources.pResolvedDepth->GetHeight(), FORWARD_PLUS_BLOCK_SIZE);
			Data.NumThreadGroups.z = 1;
			Data.ScreenDimensionsInv = Vector2(1.0f / resources.pResolvedDepth->GetWidth(), 1.0f / resources.pResolvedDepth->GetHeight());
			Data.LightCount = resources.pLightBuffer->GetNumElements();
			Data.ProjectionInverse = resources.pCamera->GetProjectionInverse();

			context.SetRootCBV(0, Data);
			context.BindResource(1, 0, m_pLightIndexCounter->GetUAV());
			context.BindResource(1, 1, m_pLightIndexListBufferOpaque->GetUAV());
			context.BindResource(1, 2, m_pLightGridOpaque->GetUAV());
			context.BindResource(1, 3, m_pLightIndexListBufferTransparant->GetUAV());
			context.BindResource(1, 4, m_pLightGridTransparant->GetUAV());
			context.BindResource(2, 0, resources.pResolvedDepth->GetSRV());
			context.BindResource(2, 1, resources.pLightBuffer->GetSRV());

			context.Dispatch(Data.NumThreadGroups);
		});

	//5. BASE PASS
	// - Render the scene using the shadow mapping result and the light culling buffers
	RGPassBuilder basePass = graph.AddPass("Base Pass");
	basePass.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
		{
			struct PerFrameData
			{
				Matrix View;
				Matrix Projection;
				Matrix ProjectionInverse;
				Matrix ViewProjection;
				Matrix ReprojectionMatrix;
				Vector4 ViewPosition;
				Vector4 FrustumPlanes[6];
				Vector2 InvScreenDimensions;
				float NearZ;
				float FarZ;
				int FrameIndex;
				int SsrSamples;
				int LightCount;
				int padd;
			} frameData;

			//Camera constants
			DirectX::XMVECTOR nearPlane, farPlane, left, right, top, bottom;
			resources.pCamera->GetFrustum().GetPlanes(&nearPlane, &farPlane, &right, &left, &top, &bottom);
			frameData.FrustumPlanes[0] = Vector4(nearPlane);
			frameData.FrustumPlanes[1] = Vector4(farPlane);
			frameData.FrustumPlanes[2] = Vector4(left);
			frameData.FrustumPlanes[3] = Vector4(right);
			frameData.FrustumPlanes[4] = Vector4(top);
			frameData.FrustumPlanes[5] = Vector4(bottom);
			frameData.View = resources.pCamera->GetView();
			frameData.Projection = resources.pCamera->GetProjection();
			frameData.ProjectionInverse = resources.pCamera->GetProjectionInverse();
			frameData.InvScreenDimensions = Vector2(1.0f / resources.pRenderTarget->GetWidth(), 1.0f / resources.pRenderTarget->GetHeight());
			frameData.NearZ = resources.pCamera->GetNear();
			frameData.FarZ = resources.pCamera->GetFar();
			frameData.FrameIndex = resources.FrameIndex;
			frameData.SsrSamples = Tweakables::g_SsrSamples;
			frameData.LightCount = resources.pLightBuffer->GetNumElements();
			frameData.ViewProjection = resources.pCamera->GetViewProjection();
			frameData.ViewPosition = Vector4(resources.pCamera->GetPosition());

			Matrix reprojectionMatrix = resources.pCamera->GetViewProjection().Invert() * resources.pCamera->GetPreviousViewProjection();
			// Transform from uv to clip space: texcoord * 2 - 1
			Matrix premult = {
				2.0f, 0, 0, 0,
				0, -2.0f, 0, 0,
				0, 0, 1, 0,
				-1, 1, 0, 1
			};
			// Transform from clip to uv space: texcoord * 0.5 + 0.5
			Matrix postmult = {
				0.5f, 0, 0, 0,
				0, -0.5f, 0, 0,
				0, 0, 1, 0,
				0.5f, 0.5f, 0, 1
			};
			frameData.ReprojectionMatrix = premult * reprojectionMatrix * postmult;

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

			context.SetRootCBV(1, frameData);
			context.SetRootCBV(2, *resources.pShadowData);
			

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
					resources.pMaterialBuffer->GetSRV()->GetDescriptor(),
					resources.pMaterialBuffer->GetSRV()->GetDescriptor(),
					resources.pMeshBuffer->GetSRV()->GetDescriptor(),
					resources.pMeshInstanceBuffer->GetSRV()->GetDescriptor(),
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
					resources.pMaterialBuffer->GetSRV()->GetDescriptor(),
					resources.pMaterialBuffer->GetSRV()->GetDescriptor(),
					resources.pMeshBuffer->GetSRV()->GetDescriptor(),
					resources.pMeshInstanceBuffer->GetSRV()->GetDescriptor(),
				};
				context.BindResources(3, 0, srvs, ARRAYSIZE(srvs));

				context.SetPipelineState(m_pDiffuseAlphaPSO);
				DrawScene(context, resources, Batch::Blending::AlphaBlend);
			}
			context.EndRenderPass();
		});
}

void TiledForward::VisualizeLightDensity(RGGraph& graph, GraphicsDevice* pDevice, Camera& camera, Texture* pTarget, Texture* pDepth)
{
	if (!m_pVisualizationIntermediateTexture || m_pVisualizationIntermediateTexture->GetDesc() != pTarget->GetDesc())
	{
		m_pVisualizationIntermediateTexture = m_pDevice->CreateTexture(pTarget->GetDesc(), "LightDensity Debug Texture");
	}

	Vector2 screenDimensions((float)pTarget->GetWidth(), (float)pTarget->GetHeight());
	float nearZ = camera.GetNear();
	float farZ = camera.GetFar();
	float sliceMagicA = 0;
	float sliceMagicB = 0;

	RGPassBuilder basePass = graph.AddPass("Visualize Light Density");
	basePass.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
		{
			struct Data
			{
				Matrix ProjectionInverse;
				IntVector3 ClusterDimensions;
				float padding;
				IntVector2 ClusterSize;
				float SliceMagicA;
				float SliceMagicB;
				float Near;
				float Far;
				float FoV;
			} constantData{};

			constantData.ProjectionInverse = camera.GetProjectionInverse();
			constantData.SliceMagicA = sliceMagicA;
			constantData.SliceMagicB = sliceMagicB;
			constantData.Near = nearZ;
			constantData.Far = farZ;
			constantData.FoV = camera.GetFoV();

			context.SetPipelineState(m_pVisualizeLightsPSO);
			context.SetComputeRootSignature(m_pVisualizeLightsRS.get());

			context.InsertResourceBarrier(pTarget, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(pDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pLightGridOpaque.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pVisualizationIntermediateTexture.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			context.SetRootCBV(0, constantData);

			context.BindResource(1, 0, pTarget->GetSRV());
			context.BindResource(1, 1, pDepth->GetSRV());
			context.BindResource(1, 2, m_pLightGridOpaque->GetSRV());

			context.BindResource(2, 0, m_pVisualizationIntermediateTexture->GetUAV());

			context.Dispatch(Math::DivideAndRoundUp(pTarget->GetWidth(), 16), Math::DivideAndRoundUp(pTarget->GetHeight(), 16));
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
