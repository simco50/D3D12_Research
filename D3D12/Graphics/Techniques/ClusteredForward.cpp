#include "stdafx.h"
#include "ClusteredForward.h"
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
#include "Graphics/SceneView.h"
#include "Scene/Camera.h"
#include "Core/ConsoleVariables.h"

static constexpr int gLightClusterTexelSize = 64;
static constexpr int gLightClustersNumZ = 32;

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
	TextureDesc volumeDesc = TextureDesc::Create3D(
		Math::DivideAndRoundUp(windowWidth, gVolumetricFroxelTexelSize),
		Math::DivideAndRoundUp(windowHeight, gVolumetricFroxelTexelSize),
		gVolumetricNumZSlices,
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		TextureFlag::ShaderResource | TextureFlag::UnorderedAccess);

	m_pAABBs = m_pDevice->CreateBuffer(BufferDesc::CreateStructured(totalClusterCount, sizeof(Vector4) * 2), "AABBs");
	m_pUniqueClusters = m_pDevice->CreateBuffer(BufferDesc::CreateStructured(totalClusterCount, sizeof(uint32)), "Unique Clusters");
	m_pUniqueClustersRawUAV = nullptr;
	m_pUniqueClusters->CreateUAV(&m_pUniqueClustersRawUAV, BufferUAVDesc::CreateRaw());

	m_pCompactedClusters = m_pDevice->CreateBuffer(BufferDesc::CreateStructured(totalClusterCount, sizeof(uint32)), "Compacted Clusters");
	m_pCompactedClustersRawUAV = nullptr;
	m_pCompactedClusters->CreateUAV(&m_pCompactedClustersRawUAV, BufferUAVDesc::CreateRaw());
	m_pDebugCompactedClusters = m_pDevice->CreateBuffer(BufferDesc::CreateStructured(totalClusterCount, sizeof(uint32)), "Debug Compacted Clusters");
	m_pIndirectArguments = m_pDevice->CreateBuffer(BufferDesc::CreateIndirectArguments<uint32>(3), "Light Culling Indirect Arguments");
	m_pLightIndexCounter = m_pDevice->CreateBuffer(BufferDesc::CreateByteAddress(sizeof(uint32)), "Light Index Counter");
	m_pLightIndexGrid = m_pDevice->CreateBuffer(BufferDesc::CreateStructured(32 * totalClusterCount, sizeof(uint32)), "Light Index Grid");
	m_pLightGrid = m_pDevice->CreateBuffer(BufferDesc::CreateStructured(totalClusterCount, 2 * sizeof(uint32)), "Light Grid");
	m_pLightGridRawUAV = nullptr;
	m_pLightGrid->CreateUAV(&m_pLightGridRawUAV, BufferUAVDesc::CreateRaw());
	m_pDebugLightGrid = m_pDevice->CreateBuffer(BufferDesc::CreateStructured(totalClusterCount, 2 * sizeof(uint32)), "Debug Light Grid");

	m_pLightScatteringVolume[0] = m_pDevice->CreateTexture(volumeDesc, "Light Scattering Volume");
	m_pLightScatteringVolume[1] = m_pDevice->CreateTexture(volumeDesc, "Light Scattering Volume");
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
		RGPassBuilder calculateAabbs = graph.AddPass("Create AABBs");
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

				context.SetComputeDynamicConstantBufferView(0, constantBuffer);
				context.BindResource(1, 0, m_pAABBs->GetUAV());

				//Cluster count in z is 32 so fits nicely in a wavefront on Nvidia so make groupsize in shader 32
				context.Dispatch(m_ClusterCountX, m_ClusterCountY, Math::DivideAndRoundUp(gLightClustersNumZ, 32));
			});
		m_ViewportDirty = false;
	}

	RGPassBuilder markClusters = graph.AddPass("Mark Clusters");
	markClusters.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
		{
			context.InsertResourceBarrier(resources.pRenderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
			context.InsertResourceBarrier(resources.pDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ);
			context.InsertResourceBarrier(m_pUniqueClusters.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			context.ClearUavUInt(m_pUniqueClusters.get(), m_pUniqueClustersRawUAV);

			context.BeginRenderPass(RenderPassInfo(resources.pDepthBuffer, RenderPassAccess::Load_Store, true));

			context.SetGraphicsRootSignature(m_pMarkUniqueClustersRS.get());
			context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			struct PerFrameParameters
			{
				IntVector3 ClusterDimensions;
				int padding0;
				IntVector2 ClusterSize;
				Vector2 LightGridParams;
				Matrix View;
				Matrix ViewProjection;
			} perFrameParameters{};


			perFrameParameters.LightGridParams = lightGridParams;
			perFrameParameters.ClusterDimensions = IntVector3(m_ClusterCountX, m_ClusterCountY, gLightClustersNumZ);
			perFrameParameters.ClusterSize = IntVector2(gLightClusterTexelSize, gLightClusterTexelSize);
			perFrameParameters.View = resources.pCamera->GetView();
			perFrameParameters.ViewProjection = resources.pCamera->GetViewProjection();

			context.SetGraphicsDynamicConstantBufferView(1, perFrameParameters);
			context.BindResource(2, 0, resources.pMeshBuffer->GetSRV());
			context.BindResource(3, 0, m_pUniqueClusters->GetUAV());
			context.BindResourceTable(4, resources.GlobalSRVHeapHandle.GpuHandle, CommandListContext::Graphics);

			{
				GPU_PROFILE_SCOPE("Opaque", &context);
				context.SetPipelineState(m_pMarkUniqueClustersOpaquePSO);
				DrawScene(context, resources, Batch::Blending::Opaque | Batch::Blending::AlphaMask);
			}

			{
				GPU_PROFILE_SCOPE("Transparant", &context);
				context.SetPipelineState(m_pMarkUniqueClustersTransparantPSO);
				DrawScene(context, resources, Batch::Blending::AlphaMask);
			}
			context.EndRenderPass();
		});

	RGPassBuilder compactClusters = graph.AddPass("Compact Clusters");
	compactClusters.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
		{
			context.SetPipelineState(m_pCompactClustersPSO);
			context.SetComputeRootSignature(m_pCompactClustersRS.get());

			context.InsertResourceBarrier(m_pUniqueClusters.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pCompactedClusters.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			UnorderedAccessView* pCompactedClustersUAV = m_pCompactedClusters->GetUAV();
			context.InsertResourceBarrier(pCompactedClustersUAV->GetCounter(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			context.ClearUavUInt(m_pCompactedClusters.get(), m_pCompactedClustersRawUAV);
			context.ClearUavUInt(pCompactedClustersUAV->GetCounter(), pCompactedClustersUAV->GetCounterUAV());

			context.BindResource(0, 0, m_pUniqueClusters->GetSRV());
			context.BindResource(1, 0, m_pCompactedClusters->GetUAV());

			context.Dispatch(Math::RoundUp(m_ClusterCountX * m_ClusterCountY * gLightClustersNumZ / 64.0f));
		});

	RGPassBuilder updateArguments = graph.AddPass("Update Indirect Arguments");
	updateArguments.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
		{
			UnorderedAccessView* pCompactedClustersUAV = m_pCompactedClusters->GetUAV();
			context.InsertResourceBarrier(pCompactedClustersUAV->GetCounter(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pIndirectArguments.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			context.SetPipelineState(m_pUpdateIndirectArgumentsPSO);
			context.SetComputeRootSignature(m_pUpdateIndirectArgumentsRS.get());

			context.BindResource(0, 0, m_pCompactedClusters->GetUAV()->GetCounter()->GetSRV());
			context.BindResource(1, 0, m_pIndirectArguments->GetUAV());

			context.Dispatch(1);
		});


	RGPassBuilder lightCulling = graph.AddPass("Clustered Light Culling");
	lightCulling.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
		{
			context.SetPipelineState(m_pLightCullingPSO);
			context.SetComputeRootSignature(m_pLightCullingRS.get());

			context.InsertResourceBarrier(m_pIndirectArguments.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
			context.InsertResourceBarrier(m_pCompactedClusters.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pAABBs.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pLightGrid.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			context.InsertResourceBarrier(m_pLightIndexGrid.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			context.InsertResourceBarrier(resources.pLightBuffer, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);

			context.ClearUavUInt(m_pLightGrid.get(), m_pLightGridRawUAV);
			context.ClearUavUInt(m_pLightIndexCounter.get(), m_pLightIndexCounter->GetUAV());

			struct ConstantBuffer
			{
				Matrix View;
				int LightCount;
			} constantBuffer{};

			constantBuffer.View = resources.pCamera->GetView();
			constantBuffer.LightCount = resources.pLightBuffer->GetNumElements();

			context.SetComputeDynamicConstantBufferView(0, constantBuffer);

			context.BindResource(1, 0, resources.pLightBuffer->GetSRV());
			context.BindResource(1, 1, m_pAABBs->GetSRV());
			context.BindResource(1, 2, m_pCompactedClusters->GetSRV());

			context.BindResource(2, 0, m_pLightIndexCounter->GetUAV());
			context.BindResource(2, 1, m_pLightIndexGrid->GetUAV());
			context.BindResource(2, 2, m_pLightGrid->GetUAV());

			context.ExecuteIndirect(m_pLightCullingCommandSignature.get(), 1, m_pIndirectArguments.get(), nullptr);
		});

	{
		RG_GRAPH_SCOPE("Volumetric Lighting", graph);

		Texture* pSourceVolume = m_pLightScatteringVolume[resources.FrameIndex % 2].get();
		Texture* pDestinationVolume = m_pLightScatteringVolume[(resources.FrameIndex + 1) % 2].get();

		struct ShaderData
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

		RGPassBuilder injectVolumeLighting = graph.AddPass("Inject Volume Lights");
		injectVolumeLighting.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
			{
				context.InsertResourceBarrier(pSourceVolume, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				context.InsertResourceBarrier(pDestinationVolume, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

				context.SetComputeRootSignature(m_pVolumetricLightingRS.get());
				context.SetPipelineState(m_pInjectVolumeLightPSO);

				D3D12_CPU_DESCRIPTOR_HANDLE srvs[] = {
					pSourceVolume->GetSRV()->GetDescriptor(),
					resources.pLightBuffer->GetSRV()->GetDescriptor(),
					resources.pAO->GetSRV()->GetDescriptor(),
					resources.pResolvedDepth->GetSRV()->GetDescriptor(),
				};

				context.SetComputeDynamicConstantBufferView(0, constantBuffer);
				context.SetComputeDynamicConstantBufferView(1, *resources.pShadowData);
				context.BindResource(2, 0, pDestinationVolume->GetUAV());
				context.BindResources(3, 0, srvs, ARRAYSIZE(srvs));
				context.BindResourceTable(4, resources.GlobalSRVHeapHandle.GpuHandle, CommandListContext::Compute);

				context.Dispatch(ComputeUtils::GetNumThreadGroups(pDestinationVolume->GetWidth(), 8, pDestinationVolume->GetHeight(), 8, pDestinationVolume->GetDepth(), 4));
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
					pDestinationVolume->GetSRV()->GetDescriptor(),
					resources.pLightBuffer->GetSRV()->GetDescriptor(),
					resources.pAO->GetSRV()->GetDescriptor(),
					resources.pResolvedDepth->GetSRV()->GetDescriptor(),
				};

				context.SetComputeDynamicConstantBufferView(0, constantBuffer);
				context.SetComputeDynamicConstantBufferView(1, *resources.pShadowData);
				context.BindResource(2, 0, m_pFinalVolumeFog->GetUAV());
				context.BindResources(3, 0, srvs, ARRAYSIZE(srvs));
				context.BindResourceTable(4, resources.GlobalSRVHeapHandle.GpuHandle, CommandListContext::Compute);

				context.Dispatch(ComputeUtils::GetNumThreadGroups(pDestinationVolume->GetWidth(), 8, pDestinationVolume->GetHeight(), 8));
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
			renderPass.RenderTargets[1].Access = RenderPassAccess::Clear_Resolve;
			renderPass.RenderTargets[1].Target = resources.pNormals;
			renderPass.RenderTargets[1].ResolveTarget = resources.pResolvedNormals;
			context.BeginRenderPass(renderPass);

			context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context.SetGraphicsRootSignature(m_pDiffuseRS.get());

			context.SetGraphicsDynamicConstantBufferView(1, frameData);
			context.SetGraphicsDynamicConstantBufferView(2, *resources.pShadowData);
			context.BindResourceTable(3, resources.GlobalSRVHeapHandle.GpuHandle, CommandListContext::Graphics);

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
			};
			context.BindResources(4, 0, srvs, ARRAYSIZE(srvs));

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
					context.CopyTexture(m_pCompactedClusters.get(), m_pDebugCompactedClusters.get());
					context.CopyTexture(m_pLightGrid.get(), m_pDebugLightGrid.get());
					m_DebugClustersViewMatrix = resources.pCamera->GetView();
					m_DebugClustersViewMatrix.Invert(m_DebugClustersViewMatrix);
					m_DidCopyDebugClusterData = true;
				}

				context.BeginRenderPass(RenderPassInfo(resources.pRenderTarget, RenderPassAccess::Load_Store, resources.pDepthBuffer, RenderPassAccess::Load_Store, false));

				context.SetPipelineState(m_pDebugClustersPSO);
				context.SetGraphicsRootSignature(m_pDebugClustersRS.get());

				Matrix p = m_DebugClustersViewMatrix * resources.pCamera->GetViewProjection();

				context.SetGraphicsDynamicConstantBufferView(0, p);
				context.BindResource(1, 0, m_pAABBs->GetSRV());
				context.BindResource(1, 1, m_pDebugCompactedClusters->GetSRV());
				context.BindResource(1, 2, m_pDebugLightGrid->GetSRV());
				context.BindResource(1, 3, m_pHeatMapTexture->GetSRV());

				if (m_pDebugClustersPSO->GetType() == PipelineStateType::Mesh)
				{
					context.DispatchMesh(m_ClusterCountX * m_ClusterCountY * gLightClustersNumZ);
				}
				else
				{
					context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
					context.Draw(0, m_ClusterCountX * m_ClusterCountY * gLightClustersNumZ);
				}

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
		m_pVisualizationIntermediateTexture = m_pDevice->CreateTexture(pTarget->GetDesc(), "LightDensity Debug Texture");
	}

	Vector2 screenDimensions((float)pTarget->GetWidth(), (float)pTarget->GetHeight());
	float nearZ = camera.GetNear();
	float farZ = camera.GetFar();
	Vector2 lightGridParams = ComputeVolumeGridParams(nearZ, farZ, gLightClustersNumZ);

	RGPassBuilder basePass = graph.AddPass("Visualize Light Density");
	basePass.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
		{
			struct Data
			{
				Matrix ProjectionInverse;
				IntVector3 ClusterDimensions;
				float padding;
				IntVector2 ClusterSize;
				Vector2 LightGridParams;
				float Near;
				float Far;
				float FoV;

			} constantData{};

			constantData.ProjectionInverse = camera.GetProjectionInverse();
			constantData.ClusterDimensions = IntVector3(m_ClusterCountX, m_ClusterCountY, gLightClustersNumZ);
			constantData.ClusterSize = IntVector2(gLightClusterTexelSize, gLightClusterTexelSize);
			constantData.Near = nearZ;
			constantData.Far = farZ;
			constantData.FoV = camera.GetFoV();
			constantData.LightGridParams = lightGridParams;

			context.SetPipelineState(m_pVisualizeLightsPSO);
			context.SetComputeRootSignature(m_pVisualizeLightsRS.get());

			context.InsertResourceBarrier(pTarget, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(pDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pLightGrid.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pVisualizationIntermediateTexture.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			context.SetComputeDynamicConstantBufferView(0, constantData);

			context.BindResource(1, 0, pTarget->GetSRV());
			context.BindResource(1, 1, pDepth->GetSRV());
			context.BindResource(1, 2, m_pLightGrid->GetSRV());

			context.BindResource(2, 0, m_pVisualizationIntermediateTexture->GetUAV());

			context.Dispatch(Math::DivideAndRoundUp(pTarget->GetWidth(), 16), Math::DivideAndRoundUp(pTarget->GetHeight(), 16));
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

	//Mark Clusters
	{
		Shader* pVertexShader = m_pDevice->GetShader("ClusterMarking.hlsl", ShaderType::Vertex, "MarkClusters_VS");
		Shader* pPixelShaderOpaque = m_pDevice->GetShader("ClusterMarking.hlsl", ShaderType::Pixel, "MarkClusters_PS");

		m_pMarkUniqueClustersRS = std::make_unique<RootSignature>(m_pDevice);
		m_pMarkUniqueClustersRS->FinalizeFromShader("Mark Unique Clusters", pVertexShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_EQUAL);
		psoDesc.SetRootSignature(m_pMarkUniqueClustersRS->GetRootSignature());
		psoDesc.SetVertexShader(pVertexShader);
		psoDesc.SetPixelShader(pPixelShaderOpaque);
		psoDesc.SetDepthOnlyTarget(GraphicsDevice::DEPTH_STENCIL_FORMAT, /* m_pDevice->GetMultiSampleCount() */ 1);
		psoDesc.SetDepthWrite(false);
		psoDesc.SetName("Mark Unique Opaque Clusters");
		m_pMarkUniqueClustersOpaquePSO = m_pDevice->CreatePipeline(psoDesc);

		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
		psoDesc.SetName("Mark Unique Transparent Clusters");
		m_pMarkUniqueClustersTransparantPSO = m_pDevice->CreatePipeline(psoDesc);
	}

	//Compact Clusters
	{
		Shader* pComputeShader = m_pDevice->GetShader("ClusterCompaction.hlsl", ShaderType::Compute, "CompactClusters");

		m_pCompactClustersRS = std::make_unique<RootSignature>(m_pDevice);
		m_pCompactClustersRS->FinalizeFromShader("Compact Clusters", pComputeShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetRootSignature(m_pCompactClustersRS->GetRootSignature());
		psoDesc.SetName("Compact Clusters");
		m_pCompactClustersPSO = m_pDevice->CreatePipeline(psoDesc);
	}

	//Prepare Indirect Dispatch Buffer
	{
		Shader* pComputeShader = m_pDevice->GetShader("ClusteredLightCullingArguments.hlsl", ShaderType::Compute, "UpdateIndirectArguments");

		m_pUpdateIndirectArgumentsRS = std::make_unique<RootSignature>(m_pDevice);
		m_pUpdateIndirectArgumentsRS->FinalizeFromShader("Update Indirect Dispatch Buffer", pComputeShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetRootSignature(m_pUpdateIndirectArgumentsRS->GetRootSignature());
		psoDesc.SetName("Update Indirect Dispatch Buffer");
		m_pUpdateIndirectArgumentsPSO = m_pDevice->CreatePipeline(psoDesc);
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
		Shader* pPixelShader = m_pDevice->GetShader("ClusterDebugDrawing.hlsl", ShaderType::Pixel, "PSMain");

		m_pDebugClustersRS = std::make_unique<RootSignature>(m_pDevice);

		PipelineStateInitializer psoDesc;
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
		psoDesc.SetDepthWrite(false);
		psoDesc.SetPixelShader(pPixelShader);
		psoDesc.SetRenderTargetFormat(GraphicsDevice::RENDER_TARGET_FORMAT, GraphicsDevice::DEPTH_STENCIL_FORMAT, /* m_pDevice->GetMultiSampleCount() */ 1);
		psoDesc.SetBlendMode(BlendMode::Additive, false);

		if (m_pDevice->GetCapabilities().SupportsRaytracing())
		{
			Shader* pMeshShader = m_pDevice->GetShader("ClusterDebugDrawing.hlsl", ShaderType::Mesh, "MSMain");
			m_pDebugClustersRS->FinalizeFromShader("Debug Clusters", pMeshShader);

			psoDesc.SetRootSignature(m_pDebugClustersRS->GetRootSignature());
			psoDesc.SetMeshShader(pMeshShader);
			psoDesc.SetName("Debug Clusters");
		}
		else
		{
			Shader* pVertexShader = m_pDevice->GetShader("ClusterDebugDrawing.hlsl", ShaderType::Vertex, "VSMain");
			Shader* pGeometryShader = m_pDevice->GetShader("ClusterDebugDrawing.hlsl", ShaderType::Geometry, "GSMain");
			m_pDebugClustersRS->FinalizeFromShader("Debug Clusters", pVertexShader);

			psoDesc.SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT);
			psoDesc.SetRootSignature(m_pDebugClustersRS->GetRootSignature());
			psoDesc.SetVertexShader(pVertexShader);
			psoDesc.SetGeometryShader(pGeometryShader);
			psoDesc.SetName("Debug Clusters");
		}
		m_pDebugClustersPSO = m_pDevice->CreatePipeline(psoDesc);
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
