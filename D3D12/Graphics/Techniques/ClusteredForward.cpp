#include "stdafx.h"
#include "ClusteredForward.h"
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
#include "Graphics/SceneView.h"
#include "Scene/Camera.h"
#include "Core/ConsoleVariables.h"

static constexpr int gLightClusterTexelSize = 64;
static constexpr int gLightClustersNumZ = 32;
static constexpr int gMaxLightsPerCluster = 32;

static constexpr int gVolumetricFroxelTexelSize = 8;
static constexpr int gVolumetricNumZSlices = 128;

namespace Tweakables
{
	extern ConsoleVariable<int> g_SsrSamples;
}
bool g_VisualizeClusters = false;

ClusteredForward::ClusteredForward(GraphicsDevice* pDevice)
	: m_pDevice(pDevice)
{
	SetupPipelines();

	CommandContext* pContext = pDevice->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
	m_pHeatMapTexture = std::make_unique<Texture>(pDevice, "Heatmap Texture");
	m_pHeatMapTexture->Create(pContext, "Resources/Textures/Heatmap.png");
	pContext->Execute(true);
}

ClusteredForward::~ClusteredForward()
{
}

void ClusteredForward::OnResize(int windowWidth, int windowHeight)
{
	m_ClusterCountX = Math::RoundUp((float)windowWidth / gLightClusterTexelSize);
	m_ClusterCountY = Math::RoundUp((float)windowHeight / gLightClusterTexelSize);

	uint32 totalClusterCount = m_ClusterCountX * m_ClusterCountY * gLightClustersNumZ;
	m_pAABBs = m_pDevice->CreateBuffer(BufferDesc::CreateStructured(totalClusterCount, sizeof(Vector4) * 2), "AABBs");

	m_pLightIndexGrid = m_pDevice->CreateBuffer(BufferDesc::CreateStructured(gMaxLightsPerCluster * totalClusterCount, sizeof(uint32)), "Light Index Grid");
	// LightGrid.x : Offset
	// LightGrid.y : Count
	m_pLightGrid = m_pDevice->CreateBuffer(BufferDesc::CreateStructured(2 * totalClusterCount, sizeof(uint32)), "Light Grid");
	m_pLightGridRawUAV = nullptr;
	m_pLightGrid->CreateUAV(&m_pLightGridRawUAV, BufferUAVDesc::CreateRaw());
	m_pDebugLightGrid = m_pDevice->CreateBuffer(m_pLightGrid->GetDesc(), "Debug Light Grid");

	TextureDesc volumeDesc = TextureDesc::Create3D(
		Math::DivideAndRoundUp(windowWidth, gVolumetricFroxelTexelSize),
		Math::DivideAndRoundUp(windowHeight, gVolumetricFroxelTexelSize),
		gVolumetricNumZSlices,
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		TextureFlag::ShaderResource | TextureFlag::UnorderedAccess);

	m_pLightScatteringVolume[0] = m_pDevice->CreateTexture(volumeDesc, "Light Scattering Volume 0");
	m_pLightScatteringVolume[1] = m_pDevice->CreateTexture(volumeDesc, "Light Scattering Volume 1");
	m_pFinalVolumeFog = m_pDevice->CreateTexture(volumeDesc, "Final Light Scattering Volume");

	m_ViewportDirty = true;
}

Vector2 ComputeVolumeGridParams(float nearZ, float farZ, int numSlices)
{
	Vector2 lightGridParams;
	float n = Math::Min(nearZ, farZ);
	float f = Math::Max(nearZ, farZ);
	lightGridParams.x = (float)numSlices / log(f / n);
	lightGridParams.y = ((float)numSlices * log(n)) / log(f / n);
	return lightGridParams;
}

void ClusteredForward::Execute(RGGraph& graph, const SceneView& resources)
{
	RG_GRAPH_SCOPE("Clustered Lighting", graph);

	Vector2 screenDimensions((float)resources.pRenderTarget->GetWidth(), (float)resources.pRenderTarget->GetHeight());
	float nearZ = resources.pCamera->GetNear();
	float farZ = resources.pCamera->GetFar();
	Vector2 lightGridParams = ComputeVolumeGridParams(nearZ, farZ, gLightClustersNumZ);

	if (m_ViewportDirty)
	{
		RGPassBuilder calculateAabbs = graph.AddPass("Cluster AABBs");
		calculateAabbs.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
			{
				context.InsertResourceBarrier(m_pAABBs.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

				context.SetPipelineState(m_pCreateAabbPSO);
				context.SetComputeRootSignature(m_pCreateAabbRS.get());

				struct ConstantBuffer
				{
					Matrix ProjectionInverse;
					Vector2 ScreenDimensionsInv;
					IntVector2 ClusterSize;
					IntVector3 ClusterDimensions;
					float NearZ;
					float FarZ;
				} constantBuffer;

				constantBuffer.ScreenDimensionsInv = Vector2(1.0f / screenDimensions.x, 1.0f / screenDimensions.y);
				constantBuffer.NearZ = resources.pCamera->GetFar();
				constantBuffer.FarZ = resources.pCamera->GetNear();
				constantBuffer.ProjectionInverse = resources.pCamera->GetProjectionInverse();
				constantBuffer.ClusterSize = IntVector2(gLightClusterTexelSize, gLightClusterTexelSize);
				constantBuffer.ClusterDimensions = IntVector3(m_ClusterCountX, m_ClusterCountY, gLightClustersNumZ);

				context.SetRootCBV(0, constantBuffer);
				context.BindResource(1, 0, m_pAABBs->GetUAV());

				//Cluster count in z is 32 so fits nicely in a wavefront on Nvidia so make groupsize in shader 32
				constexpr uint32 threadGroupSize = 32;
				context.Dispatch(
					ComputeUtils::GetNumThreadGroups(
						m_ClusterCountX, 1,
						m_ClusterCountY, 1,
						gLightClustersNumZ, threadGroupSize)
				);
			});
		m_ViewportDirty = false;
	}

	RGPassBuilder lightCulling = graph.AddPass("Light Culling");
	lightCulling.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
		{
			context.SetPipelineState(m_pLightCullingPSO);
			context.SetComputeRootSignature(m_pLightCullingRS.get());

			context.InsertResourceBarrier(m_pAABBs.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pLightGrid.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			context.InsertResourceBarrier(m_pLightIndexGrid.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			context.InsertResourceBarrier(resources.pLightBuffer, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);

			// Clear the light grid because we're accumulating the light count in the shader
			context.ClearUavUInt(m_pLightGrid.get(), m_pLightGridRawUAV);

			struct ConstantBuffer
			{
				Matrix View;
				IntVector3 ClusterDimensions;
				int LightCount;
			} constantBuffer{};

			constantBuffer.View = resources.pCamera->GetView();
			constantBuffer.ClusterDimensions = IntVector3(m_ClusterCountX, m_ClusterCountY, gLightClustersNumZ);
			constantBuffer.LightCount = resources.pLightBuffer->GetNumElements();

			context.SetRootCBV(0, constantBuffer);

			context.BindResource(1, 0, resources.pLightBuffer->GetSRV());
			context.BindResource(1, 1, m_pAABBs->GetSRV());

			context.BindResource(2, 0, m_pLightIndexGrid->GetUAV());
			context.BindResource(2, 1, m_pLightGrid->GetUAV());

			constexpr uint32 threadGroupSize = 4;
			context.Dispatch(
				ComputeUtils::GetNumThreadGroups(
					m_ClusterCountX, threadGroupSize,
					m_ClusterCountY, threadGroupSize,
					gLightClustersNumZ, threadGroupSize)
			);
		});

	{
		RG_GRAPH_SCOPE("Volumetric Lighting", graph);

		Texture* pSourceVolume = m_pLightScatteringVolume[resources.FrameIndex % 2].get();
		Texture* pDestinationVolume = m_pLightScatteringVolume[(resources.FrameIndex + 1) % 2].get();

		struct ConstantBuffer
		{
			Matrix ViewProjectionInv;
			Matrix Projection;
			Matrix PrevViewProjection;
			IntVector3 ClusterDimensions;
			int NumLights;
			Vector3 InvClusterDimensions;
			float NearZ;
			Vector3 ViewLocation;
			float FarZ;
			float Jitter;
			float LightClusterSizeFactor;
			Vector2 LightGridParams;
			IntVector3 LightClusterDimensions;
		} constantBuffer{};

		constantBuffer.ClusterDimensions = IntVector3(pDestinationVolume->GetWidth(), pDestinationVolume->GetHeight(), pDestinationVolume->GetDepth());
		constantBuffer.InvClusterDimensions = Vector3(1.0f / pDestinationVolume->GetWidth(), 1.0f / pDestinationVolume->GetHeight(), 1.0f / pDestinationVolume->GetDepth());
		constantBuffer.ViewLocation = resources.pCamera->GetPosition();
		constantBuffer.Projection = resources.pCamera->GetProjection();
		constantBuffer.ViewProjectionInv = resources.pCamera->GetProjectionInverse() * resources.pCamera->GetViewInverse();
		constantBuffer.PrevViewProjection = resources.pCamera->GetPreviousViewProjection();
		constantBuffer.NumLights = resources.pLightBuffer->GetNumElements();
		constantBuffer.NearZ = resources.pCamera->GetNear();
		constantBuffer.FarZ = resources.pCamera->GetFar();
		constexpr Math::HaltonSequence<1024, 2> halton;
		constantBuffer.Jitter = halton[resources.FrameIndex & 1023];
		constantBuffer.LightClusterSizeFactor = (float)gVolumetricFroxelTexelSize / gLightClusterTexelSize;
		constantBuffer.LightGridParams = lightGridParams;
		constantBuffer.LightClusterDimensions = IntVector3(m_ClusterCountX, m_ClusterCountY, gLightClustersNumZ);

		RGPassBuilder injectVolumeLighting = graph.AddPass("Inject Volume Lights");
		injectVolumeLighting.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
			{
				context.InsertResourceBarrier(pSourceVolume, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				context.InsertResourceBarrier(pDestinationVolume, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

				context.SetComputeRootSignature(m_pVolumetricLightingRS.get());
				context.SetPipelineState(m_pInjectVolumeLightPSO);

				D3D12_CPU_DESCRIPTOR_HANDLE srvs[] = {
					m_pLightGrid->GetSRV()->GetDescriptor(),
					m_pLightIndexGrid->GetSRV()->GetDescriptor(),
					pSourceVolume->GetSRV()->GetDescriptor(),
					resources.pLightBuffer->GetSRV()->GetDescriptor(),
					resources.pAO->GetSRV()->GetDescriptor(),
					resources.pResolvedDepth->GetSRV()->GetDescriptor(),
				};

				context.SetRootCBV(0, constantBuffer);
				context.SetRootCBV(1, *resources.pShadowData);
				context.BindResource(2, 0, pDestinationVolume->GetUAV());
				context.BindResources(3, 0, srvs, ARRAYSIZE(srvs));

				constexpr uint32 threadGroupSizeXY = 8;
				constexpr uint32 threadGroupSizeZ = 4;

				context.Dispatch(
					ComputeUtils::GetNumThreadGroups(
						pDestinationVolume->GetWidth(), threadGroupSizeXY,
						pDestinationVolume->GetHeight(), threadGroupSizeXY,
						pDestinationVolume->GetDepth(), threadGroupSizeZ)
				);
			});

		RGPassBuilder accumulateFog = graph.AddPass("Accumulate Volume Fog");
		accumulateFog.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
			{
				context.InsertResourceBarrier(pDestinationVolume, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				context.InsertResourceBarrier(m_pFinalVolumeFog.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

				context.SetComputeRootSignature(m_pVolumetricLightingRS.get());
				context.SetPipelineState(m_pAccumulateVolumeLightPSO);

				//float values[] = { 0,0,0,0 };
				//context.ClearUavFloat(m_pFinalVolumeFog.get(), m_pFinalVolumeFog->GetUAV(), values);

				D3D12_CPU_DESCRIPTOR_HANDLE srvs[] = {
					m_pLightGrid->GetSRV()->GetDescriptor(),
					m_pLightIndexGrid->GetSRV()->GetDescriptor(),
					pDestinationVolume->GetSRV()->GetDescriptor(),
					resources.pLightBuffer->GetSRV()->GetDescriptor(),
					resources.pAO->GetSRV()->GetDescriptor(),
					resources.pResolvedDepth->GetSRV()->GetDescriptor(),
				};

				context.SetRootCBV(0, constantBuffer);
				context.SetRootCBV(1, *resources.pShadowData);
				context.BindResource(2, 0, m_pFinalVolumeFog->GetUAV());
				context.BindResources(3, 0, srvs, ARRAYSIZE(srvs));

				constexpr uint32 threadGroupSize = 8;

				context.Dispatch(
					ComputeUtils::GetNumThreadGroups(
						pDestinationVolume->GetWidth(), threadGroupSize,
						pDestinationVolume->GetHeight(), threadGroupSize));
			});
	}

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
				Vector2 InvScreenDimensions;
				float NearZ;
				float FarZ;
				int FrameIndex;
				int SsrSamples;
				int LightCount;
				int padd;
				IntVector3 ClusterDimensions;
				int pad;
				IntVector2 ClusterSize;
				Vector2 LightGridParams;
				IntVector3 VolumeFogDimensions;
			} frameData{};

			Matrix view = resources.pCamera->GetView();
			frameData.View = view;
			frameData.Projection = resources.pCamera->GetProjection();
			frameData.ProjectionInverse = resources.pCamera->GetProjectionInverse();
			frameData.InvScreenDimensions = Vector2(1.0f / screenDimensions.x, 1.0f / screenDimensions.y);
			frameData.NearZ = nearZ;
			frameData.FarZ = farZ;
			frameData.ClusterDimensions = IntVector3(m_ClusterCountX, m_ClusterCountY, gLightClustersNumZ);
			frameData.ClusterSize = IntVector2(gLightClusterTexelSize, gLightClusterTexelSize);
			frameData.LightGridParams = lightGridParams;
			frameData.FrameIndex = resources.FrameIndex;
			frameData.SsrSamples = Tweakables::g_SsrSamples;
			frameData.LightCount = resources.pLightBuffer->GetNumElements();
			frameData.ViewProjection = resources.pCamera->GetViewProjection();
			frameData.ViewPosition = Vector4(resources.pCamera->GetPosition());
			frameData.VolumeFogDimensions = IntVector3(m_pFinalVolumeFog->GetWidth(), m_pFinalVolumeFog->GetHeight(), m_pFinalVolumeFog->GetDepth());

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

			context.InsertResourceBarrier(m_pLightGrid.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pLightIndexGrid.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(resources.pAO, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(resources.pPreviousColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(resources.pResolvedDepth, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pFinalVolumeFog.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

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
			renderPass.RenderTargets[1].Access = resources.pNormals->GetDesc().SampleCount > 1 ? RenderPassAccess::Clear_Resolve : RenderPassAccess::Clear_Store;
			renderPass.RenderTargets[1].Target = resources.pNormals;
			renderPass.RenderTargets[1].ResolveTarget = resources.pResolvedNormals;
			context.BeginRenderPass(renderPass);

			context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context.SetGraphicsRootSignature(m_pDiffuseRS.get());

			context.SetRootCBV(1, frameData);
			context.SetRootCBV(2, *resources.pShadowData);

			D3D12_CPU_DESCRIPTOR_HANDLE srvs[] = {
				m_pFinalVolumeFog->GetSRV()->GetDescriptor(),
				m_pLightGrid->GetSRV()->GetDescriptor(),
				m_pLightIndexGrid->GetSRV()->GetDescriptor(),
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

			{
				GPU_PROFILE_SCOPE("Opaque", &context);
				context.SetPipelineState(m_pDiffusePSO);
				DrawScene(context, resources, Batch::Blending::Opaque);
			}
			{
				GPU_PROFILE_SCOPE("Opaque - Masked", &context);
				context.SetPipelineState(m_pDiffuseMaskedPSO);
				DrawScene(context, resources, Batch::Blending::AlphaMask);
				
			}
			{
				GPU_PROFILE_SCOPE("Transparant", &context);
				context.SetPipelineState(m_pDiffuseTransparancyPSO);
				DrawScene(context, resources, Batch::Blending::AlphaBlend);
			}

			context.EndRenderPass();
		});

	if (g_VisualizeClusters)
	{
		RGPassBuilder visualize = graph.AddPass("Visualize Clusters");
		visualize.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
			{
				if (m_DidCopyDebugClusterData == false)
				{
					context.CopyTexture(m_pLightGrid.get(), m_pDebugLightGrid.get());
					m_DebugClustersViewMatrix = resources.pCamera->GetView();
					m_DebugClustersViewMatrix.Invert(m_DebugClustersViewMatrix);
					m_DidCopyDebugClusterData = true;
				}

				context.BeginRenderPass(RenderPassInfo(resources.pRenderTarget, RenderPassAccess::Load_Store, resources.pDepthBuffer, RenderPassAccess::Load_Store, false));

				context.SetPipelineState(m_pVisualizeLightClustersPSO);
				context.SetGraphicsRootSignature(m_pVisualizeLightClustersRS.get());
				context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);

				struct ConstantBuffer
				{
					Matrix View;
				} constantBuffer;
				constantBuffer.View = m_DebugClustersViewMatrix * resources.pCamera->GetViewProjection();
				context.SetRootCBV(0, constantBuffer);

				D3D12_CPU_DESCRIPTOR_HANDLE srvs[] = {
					m_pAABBs->GetSRV()->GetDescriptor(),
					m_pDebugLightGrid->GetSRV()->GetDescriptor(),
					m_pHeatMapTexture->GetSRV()->GetDescriptor(),
				};
				context.BindResources(1, 0, srvs, ARRAYSIZE(srvs));

				context.Draw(0, m_ClusterCountX * m_ClusterCountY * gLightClustersNumZ);

				context.EndRenderPass();
			});
	}
	else
	{
		m_DidCopyDebugClusterData = false;
	}
}

void ClusteredForward::VisualizeLightDensity(RGGraph& graph, Camera& camera, Texture* pTarget, Texture* pDepth)
{
	if (!m_pVisualizationIntermediateTexture || m_pVisualizationIntermediateTexture->GetDesc() != pTarget->GetDesc())
	{
		m_pVisualizationIntermediateTexture = m_pDevice->CreateTexture(pTarget->GetDesc(), "Light Density Debug Texture");
	}

	Vector2 screenDimensions((float)pTarget->GetWidth(), (float)pTarget->GetHeight());
	float nearZ = camera.GetNear();
	float farZ = camera.GetFar();
	Vector2 lightGridParams = ComputeVolumeGridParams(nearZ, farZ, gLightClustersNumZ);

	RGPassBuilder basePass = graph.AddPass("Visualize Light Density");
	basePass.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
		{
			struct ConstantBuffer
			{
				Matrix ProjectionInverse;
				IntVector3 ClusterDimensions;
				float padding;
				IntVector2 ClusterSize;
				Vector2 LightGridParams;
				float Near;
				float Far;
				float FoV;
			} constantBuffer;

			constantBuffer.ProjectionInverse = camera.GetProjectionInverse();
			constantBuffer.ClusterDimensions = IntVector3(m_ClusterCountX, m_ClusterCountY, gLightClustersNumZ);
			constantBuffer.ClusterSize = IntVector2(gLightClusterTexelSize, gLightClusterTexelSize);
			constantBuffer.Near = nearZ;
			constantBuffer.Far = farZ;
			constantBuffer.FoV = camera.GetFoV();
			constantBuffer.LightGridParams = lightGridParams;

			context.SetPipelineState(m_pVisualizeLightsPSO);
			context.SetComputeRootSignature(m_pVisualizeLightsRS.get());

			context.InsertResourceBarrier(pTarget, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(pDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pLightGrid.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pVisualizationIntermediateTexture.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			context.SetRootCBV(0, constantBuffer);

			context.BindResource(1, 0, pTarget->GetSRV());
			context.BindResource(1, 1, pDepth->GetSRV());
			context.BindResource(1, 2, m_pLightGrid->GetSRV());

			context.BindResource(2, 0, m_pVisualizationIntermediateTexture->GetUAV());

			context.Dispatch(
				ComputeUtils::GetNumThreadGroups(
					pTarget->GetWidth(), 16,
					pTarget->GetHeight(), 16)
			);
			context.InsertUavBarrier();

			context.CopyTexture(m_pVisualizationIntermediateTexture.get(), pTarget);
		});
}

void ClusteredForward::SetupPipelines()
{
	//AABB
	{
		Shader* pComputeShader = m_pDevice->GetShader("ClusterAABBGeneration.hlsl", ShaderType::Compute, "GenerateAABBs");

		m_pCreateAabbRS = std::make_unique<RootSignature>(m_pDevice);
		m_pCreateAabbRS->FinalizeFromShader("Create AABB", pComputeShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetRootSignature(m_pCreateAabbRS->GetRootSignature());
		psoDesc.SetName("Create AABB");
		m_pCreateAabbPSO = m_pDevice->CreatePipeline(psoDesc);
	}

	//Light Culling
	{
		Shader* pComputeShader = m_pDevice->GetShader("ClusteredLightCulling.hlsl", ShaderType::Compute, "LightCulling");

		m_pLightCullingRS = std::make_unique<RootSignature>(m_pDevice);
		m_pLightCullingRS->FinalizeFromShader("Light Culling", pComputeShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetRootSignature(m_pLightCullingRS->GetRootSignature());
		psoDesc.SetName("Light Culling");
		m_pLightCullingPSO = m_pDevice->CreatePipeline(psoDesc);

		m_pLightCullingCommandSignature = std::make_unique<CommandSignature>(m_pDevice);
		m_pLightCullingCommandSignature->AddDispatch();
		m_pLightCullingCommandSignature->Finalize("Light Culling Command Signature");
	}

	//Diffuse
	{
		Shader* pVertexShader = m_pDevice->GetShader("Diffuse.hlsl", ShaderType::Vertex, "VSMain", { "CLUSTERED_FORWARD" });
		Shader* pPixelShader = m_pDevice->GetShader("Diffuse.hlsl", ShaderType::Pixel, "PSMain", { "CLUSTERED_FORWARD" });

		m_pDiffuseRS = std::make_unique<RootSignature>(m_pDevice);
		m_pDiffuseRS->FinalizeFromShader("Diffuse", pVertexShader);

		DXGI_FORMAT formats[] = {
			GraphicsDevice::RENDER_TARGET_FORMAT,
			DXGI_FORMAT_R16G16B16A16_FLOAT,
		};

		//Opaque
		PipelineStateInitializer psoDesc;
		psoDesc.SetRootSignature(m_pDiffuseRS->GetRootSignature());
		psoDesc.SetBlendMode(BlendMode::Replace, false);
		psoDesc.SetVertexShader(pVertexShader);
		psoDesc.SetPixelShader(pPixelShader);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_EQUAL);
		psoDesc.SetDepthWrite(false);
		psoDesc.SetRenderTargetFormats(formats, ARRAYSIZE(formats), GraphicsDevice::DEPTH_STENCIL_FORMAT, /* m_pDevice->GetMultiSampleCount() */ 1);
		psoDesc.SetName("Diffuse (Opaque)");
		m_pDiffusePSO = m_pDevice->CreatePipeline(psoDesc);

		//Opaque Masked
		psoDesc.SetName("Diffuse Masked (Opaque)");
		psoDesc.SetCullMode(D3D12_CULL_MODE_NONE);
		m_pDiffuseMaskedPSO = m_pDevice->CreatePipeline(psoDesc);

		//Transparant
		psoDesc.SetName("Diffuse (Transparant)");
		psoDesc.SetBlendMode(BlendMode::Alpha, false);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
		m_pDiffuseTransparancyPSO = m_pDevice->CreatePipeline(psoDesc);
	}

	//Cluster debug rendering
	{
		Shader* pVertexShader = m_pDevice->GetShader("VisualizeLightClusters.hlsl", ShaderType::Vertex, "VSMain");
		Shader* pGeometryShader = m_pDevice->GetShader("VisualizeLightClusters.hlsl", ShaderType::Geometry, "GSMain");
		Shader* pPixelShader = m_pDevice->GetShader("VisualizeLightClusters.hlsl", ShaderType::Pixel, "PSMain");

		m_pVisualizeLightClustersRS = std::make_unique<RootSignature>(m_pDevice);

		PipelineStateInitializer psoDesc;
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
		psoDesc.SetDepthWrite(false);
		psoDesc.SetPixelShader(pPixelShader);
		psoDesc.SetRenderTargetFormat(GraphicsDevice::RENDER_TARGET_FORMAT, GraphicsDevice::DEPTH_STENCIL_FORMAT, /* m_pDevice->GetMultiSampleCount() */ 1);
		psoDesc.SetBlendMode(BlendMode::Additive, false);

		m_pVisualizeLightClustersRS->FinalizeFromShader("Visualize Light Clusters", pVertexShader);

		psoDesc.SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT);
		psoDesc.SetRootSignature(m_pVisualizeLightClustersRS->GetRootSignature());
		psoDesc.SetVertexShader(pVertexShader);
		psoDesc.SetGeometryShader(pGeometryShader);
		psoDesc.SetName("Visualize Light Clusters");
		m_pVisualizeLightClustersPSO = m_pDevice->CreatePipeline(psoDesc);
	}

	{
		Shader* pComputeShader = m_pDevice->GetShader("VisualizeLightCount.hlsl", ShaderType::Compute, "DebugLightDensityCS", { "CLUSTERED_FORWARD" });

		m_pVisualizeLightsRS = std::make_unique<RootSignature>(m_pDevice);
		m_pVisualizeLightsRS->FinalizeFromShader("Light Density Visualization", pComputeShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetRootSignature(m_pVisualizeLightsRS->GetRootSignature());
		psoDesc.SetName("Light Density Visualization");
		m_pVisualizeLightsPSO = m_pDevice->CreatePipeline(psoDesc);
	}

	{
		Shader* pComputeShader = m_pDevice->GetShader("VolumetricFog.hlsl", ShaderType::Compute, "InjectFogLightingCS", { });

		m_pVolumetricLightingRS = std::make_unique<RootSignature>(m_pDevice);
		m_pVolumetricLightingRS->FinalizeFromShader("Inject Fog Lighting", pComputeShader);

		{
			PipelineStateInitializer psoDesc;
			psoDesc.SetComputeShader(pComputeShader);
			psoDesc.SetRootSignature(m_pVolumetricLightingRS->GetRootSignature());
			psoDesc.SetName("Inject Fog Lighting");
			m_pInjectVolumeLightPSO = m_pDevice->CreatePipeline(psoDesc);
		}

		{
			Shader* pAccumulateComputeShader = m_pDevice->GetShader("VolumetricFog.hlsl", ShaderType::Compute, "AccumulateFogCS", { });

			PipelineStateInitializer psoDesc;
			psoDesc.SetComputeShader(pAccumulateComputeShader);
			psoDesc.SetRootSignature(m_pVolumetricLightingRS->GetRootSignature());
			psoDesc.SetName("Accumulate Fog Lighting");
			m_pAccumulateVolumeLightPSO = m_pDevice->CreatePipeline(psoDesc);
		}

	}
}
