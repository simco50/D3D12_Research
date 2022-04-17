#include "stdafx.h"
#include "ClusteredForward.h"
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

static constexpr int gLightClusterTexelSize = 64;
static constexpr int gLightClustersNumZ = 32;
static constexpr int gMaxLightsPerCluster = 32;

static constexpr int gVolumetricFroxelTexelSize = 8;
static constexpr int gVolumetricNumZSlices = 128;

namespace Tweakables
{
	extern ConsoleVariable<int> g_SsrSamples;
	extern ConsoleVariable<bool> g_VolumetricFog;
}
bool g_VisualizeClusters = false;

ClusteredForward::ClusteredForward(GraphicsDevice* pDevice)
	: m_pDevice(pDevice)
{
	CommandContext* pContext = pDevice->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
	m_pHeatMapTexture = pDevice->CreateTextureFromFile(*pContext, "Resources/Textures/Heatmap.png", true, "Color Heatmap");
	pContext->Execute(true);

	//Light Culling
	{
		m_pLightCullingRS = new RootSignature(pDevice);
		m_pLightCullingRS->AddConstantBufferView(0);
		m_pLightCullingRS->AddConstantBufferView(100);
		m_pLightCullingRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2);
		m_pLightCullingRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2);
		m_pLightCullingRS->Finalize("Light Culling");

		m_pCreateAabbPSO = pDevice->CreateComputePipeline(m_pLightCullingRS, "ClusterAABBGeneration.hlsl", "GenerateAABBs");
		m_pLightCullingPSO = pDevice->CreateComputePipeline(m_pLightCullingRS, "ClusteredLightCulling.hlsl", "LightCulling");

		m_pLightCullingCommandSignature = new CommandSignature(pDevice);
		m_pLightCullingCommandSignature->AddDispatch();
		m_pLightCullingCommandSignature->Finalize("Light Culling Command Signature");
	}

	//Diffuse
	{
		m_pDiffuseRS = new RootSignature(pDevice);
		m_pDiffuseRS->AddRootConstants(0, 3);
		m_pDiffuseRS->AddConstantBufferView(1);
		m_pDiffuseRS->AddConstantBufferView(100);
		m_pDiffuseRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 8);
		m_pDiffuseRS->Finalize("Diffuse");

		constexpr DXGI_FORMAT formats[] = {
			DXGI_FORMAT_R16G16B16A16_FLOAT,
			DXGI_FORMAT_R16G16_FLOAT,
			DXGI_FORMAT_R8_UNORM,
		};

		{
			//Opaque
			PipelineStateInitializer psoDesc;
			psoDesc.SetRootSignature(m_pDiffuseRS);
			psoDesc.SetBlendMode(BlendMode::Replace, false);
			psoDesc.SetVertexShader("Diffuse.hlsl", "VSMain", { "CLUSTERED_FORWARD" });
			psoDesc.SetPixelShader("Diffuse.hlsl", "PSMain", { "CLUSTERED_FORWARD" });
			psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_EQUAL);
			psoDesc.SetDepthWrite(false);
			psoDesc.SetRenderTargetFormats(formats, DXGI_FORMAT_D32_FLOAT, 1);
			psoDesc.SetName("Diffuse (Opaque)");
			m_pDiffusePSO = pDevice->CreatePipeline(psoDesc);

			//Opaque Masked
			psoDesc.SetName("Diffuse Masked (Opaque)");
			psoDesc.SetCullMode(D3D12_CULL_MODE_NONE);
			m_pDiffuseMaskedPSO = pDevice->CreatePipeline(psoDesc);

			//Transparant
			psoDesc.SetName("Diffuse (Transparant)");
			psoDesc.SetBlendMode(BlendMode::Alpha, false);
			psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
			m_pDiffuseTransparancyPSO = pDevice->CreatePipeline(psoDesc);
		}

		if (pDevice->GetCapabilities().SupportsMeshShading())
		{
			//Opaque
			PipelineStateInitializer psoDesc;
			psoDesc.SetRootSignature(m_pDiffuseRS);
			psoDesc.SetBlendMode(BlendMode::Replace, false);
			psoDesc.SetMeshShader("Diffuse.hlsl", "MSMain", { "CLUSTERED_FORWARD" });
			psoDesc.SetAmplificationShader("Diffuse.hlsl", "ASMain", { "CLUSTERED_FORWARD" });
			psoDesc.SetPixelShader("Diffuse.hlsl", "PSMain", { "CLUSTERED_FORWARD" });
			psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_EQUAL);
			psoDesc.SetDepthWrite(false);
			psoDesc.SetRenderTargetFormats(formats, DXGI_FORMAT_D32_FLOAT, 1);
			psoDesc.SetName("Diffuse (Opaque)");
			m_pMeshShaderDiffusePSO = pDevice->CreatePipeline(psoDesc);

			//Opaque Masked
			psoDesc.SetName("Diffuse Masked (Opaque)");
			psoDesc.SetCullMode(D3D12_CULL_MODE_NONE);
			m_pMeshShaderDiffuseMaskedPSO = pDevice->CreatePipeline(psoDesc);

			//Transparant
			psoDesc.SetName("Diffuse (Transparant)");
			psoDesc.SetBlendMode(BlendMode::Alpha, false);
			psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
			m_pMeshShaderDiffuseTransparancyPSO = pDevice->CreatePipeline(psoDesc);
		}
	}

	//Cluster debug rendering
	{
		m_pVisualizeLightClustersRS = new RootSignature(pDevice);
		m_pVisualizeLightClustersRS->AddConstantBufferView(100);
		m_pVisualizeLightClustersRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3);
		m_pVisualizeLightClustersRS->Finalize("Visualize Light Clusters");

		PipelineStateInitializer psoDesc;
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
		psoDesc.SetDepthWrite(false);
		psoDesc.SetPixelShader("VisualizeLightClusters.hlsl", "PSMain");
		psoDesc.SetRenderTargetFormats(DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_D32_FLOAT, 1);
		psoDesc.SetBlendMode(BlendMode::Additive, false);
		psoDesc.SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT);
		psoDesc.SetRootSignature(m_pVisualizeLightClustersRS);
		psoDesc.SetVertexShader("VisualizeLightClusters.hlsl", "VSMain");
		psoDesc.SetGeometryShader("VisualizeLightClusters.hlsl", "GSMain");
		psoDesc.SetName("Visualize Light Clusters");
		m_pVisualizeLightClustersPSO = pDevice->CreatePipeline(psoDesc);
	}

	{
		m_pVisualizeLightsRS = new RootSignature(pDevice);
		m_pVisualizeLightsRS->AddConstantBufferView(0);
		m_pVisualizeLightsRS->AddConstantBufferView(100);
		m_pVisualizeLightsRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3);
		m_pVisualizeLightsRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 3);
		m_pVisualizeLightsRS->Finalize("Light Density Visualization");

		m_pVisualizeLightsPSO = pDevice->CreateComputePipeline(m_pVisualizeLightsRS, "VisualizeLightCount.hlsl", "DebugLightDensityCS", { "CLUSTERED_FORWARD" });
	}

	{
		m_pVolumetricLightingRS = new RootSignature(pDevice);
		m_pVolumetricLightingRS->AddConstantBufferView(0);
		m_pVolumetricLightingRS->AddConstantBufferView(100);
		m_pVolumetricLightingRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 3);
		m_pVolumetricLightingRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3);
		m_pVolumetricLightingRS->Finalize("Inject Fog Lighting");

		m_pInjectVolumeLightPSO = pDevice->CreateComputePipeline(m_pVolumetricLightingRS, "VolumetricFog.hlsl", "InjectFogLightingCS");
		m_pAccumulateVolumeLightPSO = pDevice->CreateComputePipeline(m_pVolumetricLightingRS, "VolumetricFog.hlsl", "AccumulateFogCS");
	}
}

ClusteredForward::~ClusteredForward()
{
}

void ClusteredForward::OnResize(int windowWidth, int windowHeight)
{
	CreateLightCullingResources(m_LightCullData, IntVector2(windowWidth, windowHeight));
	CreateVolumetricFogResources(m_VolumetricFogData, IntVector2(windowWidth, windowHeight));
}

void ClusteredForward::Execute(RGGraph& graph, const SceneView& view, const SceneTextures& sceneTextures)
{
	ComputeLightCulling(graph, view, m_LightCullData);

	Texture* pFogVolume = GraphicsCommon::GetDefaultTexture(DefaultTexture::Black3D);
	if (Tweakables::g_VolumetricFog)
	{
		RenderVolumetricFog(graph, view, m_LightCullData, m_VolumetricFogData);
		pFogVolume = m_VolumetricFogData.pFinalVolumeFog;
	}

	RenderBasePass(graph, view, sceneTextures, m_LightCullData, pFogVolume);

	if (g_VisualizeClusters)
	{
		VisualizeClusters(graph, view, sceneTextures, m_LightCullData);
	}
	else
	{
		m_LightCullData.DirtyDebugData = true;
	}
}

void ClusteredForward::CreateLightCullingResources(ClusteredLightCullData& resources, const IntVector2& viewDimensions)
{
	resources.ClusterCount.x = Math::DivideAndRoundUp(viewDimensions.x, gLightClusterTexelSize);
	resources.ClusterCount.y = Math::DivideAndRoundUp(viewDimensions.y, gLightClusterTexelSize);
	resources.ClusterCount.z = gLightClustersNumZ;

	uint32 totalClusterCount = resources.ClusterCount.x * resources.ClusterCount.y * resources.ClusterCount.z;
	resources.pAABBs = m_pDevice->CreateBuffer(BufferDesc::CreateStructured(totalClusterCount, sizeof(Vector4) * 2), "AABBs");

	resources.pLightIndexGrid = m_pDevice->CreateBuffer(BufferDesc::CreateStructured(gMaxLightsPerCluster * totalClusterCount, sizeof(uint32)), "Light Index Grid");
	// LightGrid.x : Offset
	// LightGrid.y : Count
	resources.pLightGrid = m_pDevice->CreateBuffer(BufferDesc::CreateStructured(2 * totalClusterCount, sizeof(uint32)), "Light Grid");
	resources.pLightGridRawUAV = m_pDevice->CreateUAV(resources.pLightGrid, BufferUAVDesc::CreateRaw());
	resources.pDebugLightGrid = m_pDevice->CreateBuffer(resources.pLightGrid->GetDesc(), "Debug Light Grid");
	resources.IsViewDirty = true;
}

void ClusteredForward::CreateVolumetricFogResources(VolumetricFogData& resources, const IntVector2& viewDimensions)
{
	TextureDesc volumeDesc = TextureDesc::Create3D(
		Math::DivideAndRoundUp(viewDimensions.x, gVolumetricFroxelTexelSize),
		Math::DivideAndRoundUp(viewDimensions.y, gVolumetricFroxelTexelSize),
		gVolumetricNumZSlices,
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		TextureFlag::ShaderResource | TextureFlag::UnorderedAccess);

	resources.pLightScatteringVolume[0] = m_pDevice->CreateTexture(volumeDesc, "Light Scattering Volume 0");
	resources.pLightScatteringVolume[1] = m_pDevice->CreateTexture(volumeDesc, "Light Scattering Volume 1");
	resources.pFinalVolumeFog = m_pDevice->CreateTexture(volumeDesc, "Final Light Scattering Volume");
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

void ClusteredForward::ComputeLightCulling(RGGraph& graph, const SceneView& view, ClusteredLightCullData& resources)
{
	float nearZ = view.View.NearPlane;
	float farZ = view.View.FarPlane;
	resources.LightGridParams = ComputeVolumeGridParams(nearZ, farZ, gLightClustersNumZ);

	if (resources.IsViewDirty)
	{
		resources.IsViewDirty = false;

		RGPassBuilder calculateAabbs = graph.AddPass("Cluster AABBs");
		calculateAabbs.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
			{
				context.InsertResourceBarrier(resources.pAABBs, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

				context.SetPipelineState(m_pCreateAabbPSO);
				context.SetComputeRootSignature(m_pLightCullingRS);

				struct
				{
					IntVector4 ClusterDimensions;
					IntVector2 ClusterSize;
				} constantBuffer;

				constantBuffer.ClusterSize = IntVector2(gLightClusterTexelSize, gLightClusterTexelSize);
				constantBuffer.ClusterDimensions = IntVector4(resources.ClusterCount.x, resources.ClusterCount.y, resources.ClusterCount.z, 0);

				context.SetRootCBV(0, constantBuffer);
				context.SetRootCBV(1, GetViewUniforms(view));
				context.BindResources(2, resources.pAABBs->GetUAV());

				//Cluster count in z is 32 so fits nicely in a wavefront on Nvidia so make groupsize in shader 32
				constexpr uint32 threadGroupSize = 32;
				context.Dispatch(
					ComputeUtils::GetNumThreadGroups(
						resources.ClusterCount.x, 1,
						resources.ClusterCount.y, 1,
						resources.ClusterCount.z, threadGroupSize)
				);
			});
	}

	RGPassBuilder lightCulling = graph.AddPass("Light Culling");
	lightCulling.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
		{
			context.SetPipelineState(m_pLightCullingPSO);
			context.SetComputeRootSignature(m_pLightCullingRS);

			context.InsertResourceBarrier(resources.pAABBs, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(resources.pLightGrid, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			context.InsertResourceBarrier(resources.pLightIndexGrid, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			// Clear the light grid because we're accumulating the light count in the shader
			context.ClearUavUInt(resources.pLightGrid, resources.pLightGridRawUAV);

			struct
			{
				IntVector3 ClusterDimensions;
			} constantBuffer;

			constantBuffer.ClusterDimensions = resources.ClusterCount;

			context.SetRootCBV(0, constantBuffer);

			context.SetRootCBV(1, GetViewUniforms(view));
			context.BindResources(2, {
				resources.pLightIndexGrid->GetUAV(),
				resources.pLightGrid->GetUAV()
				});
			context.BindResources(3, resources.pAABBs->GetSRV());

			context.Dispatch(
				ComputeUtils::GetNumThreadGroups(
					resources.ClusterCount.x, 4,
					resources.ClusterCount.y, 4,
					resources.ClusterCount.z, 4)
			);
		});
}

void ClusteredForward::VisualizeClusters(RGGraph& graph, const SceneView& view, const SceneTextures& sceneTextures, ClusteredLightCullData& resources)
{
	bool copyClusterData = resources.DirtyDebugData;
	if (copyClusterData)
	{
		resources.DebugClustersViewMatrix = view.View.ViewInverse;
		resources.DirtyDebugData = false;
	}

	RGPassBuilder visualize = graph.AddPass("Visualize Clusters");
	visualize.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
		{
			if (copyClusterData)
			{
				context.CopyTexture(resources.pLightGrid, resources.pDebugLightGrid);
			}

			context.BeginRenderPass(RenderPassInfo(sceneTextures.pColorTarget, RenderPassAccess::Load_Store, sceneTextures.pDepth, RenderPassAccess::Load_Store, false));

			context.SetPipelineState(m_pVisualizeLightClustersPSO);
			context.SetGraphicsRootSignature(m_pVisualizeLightClustersRS);
			context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);

			ShaderInterop::ViewUniforms viewData = GetViewUniforms(view, sceneTextures.pColorTarget);
			viewData.Projection = resources.DebugClustersViewMatrix * view.View.ViewProjection;
			context.SetRootCBV(0, viewData);
			context.BindResources(1, {
				resources.pAABBs->GetSRV(),
				resources.pDebugLightGrid->GetSRV(),
				m_pHeatMapTexture->GetSRV(),
				});
			context.Draw(0, resources.ClusterCount.x * resources.ClusterCount.y * resources.ClusterCount.z);

			context.EndRenderPass();
		});
}

void ClusteredForward::RenderVolumetricFog(RGGraph& graph, const SceneView& view, const ClusteredLightCullData& lightCullData, VolumetricFogData& fogData)
{
	RG_GRAPH_SCOPE("Volumetric Lighting", graph);

	Texture* pSourceVolume = fogData.pLightScatteringVolume[view.FrameIndex % 2];
	Texture* pDestinationVolume = fogData.pLightScatteringVolume[(view.FrameIndex + 1) % 2];

	struct
	{
		IntVector3 ClusterDimensions;
		float Jitter;
		Vector3 InvClusterDimensions;
		float LightClusterSizeFactor;
		Vector2 LightGridParams;
		IntVector2 LightClusterDimensions;
	} constantBuffer;

	constantBuffer.ClusterDimensions = IntVector3(pDestinationVolume->GetWidth(), pDestinationVolume->GetHeight(), pDestinationVolume->GetDepth());
	constantBuffer.InvClusterDimensions = Vector3(1.0f / pDestinationVolume->GetWidth(), 1.0f / pDestinationVolume->GetHeight(), 1.0f / pDestinationVolume->GetDepth());
	constexpr Math::HaltonSequence<32, 2> halton;
	constantBuffer.Jitter = halton[view.FrameIndex & 31];
	constantBuffer.LightClusterSizeFactor = (float)gVolumetricFroxelTexelSize / gLightClusterTexelSize;
	constantBuffer.LightGridParams = lightCullData.LightGridParams;
	constantBuffer.LightClusterDimensions = IntVector2(lightCullData.ClusterCount.x, lightCullData.ClusterCount.y);

	RGPassBuilder injectVolumeLighting = graph.AddPass("Inject Volume Lights");
	injectVolumeLighting.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
		{
			context.InsertResourceBarrier(pSourceVolume, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(pDestinationVolume, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			context.SetComputeRootSignature(m_pVolumetricLightingRS);
			context.SetPipelineState(m_pInjectVolumeLightPSO);

			context.SetRootCBV(0, constantBuffer);
			context.SetRootCBV(1, GetViewUniforms(view));
			context.BindResources(2, pDestinationVolume->GetUAV());
			context.BindResources(3, {
				lightCullData.pLightGrid->GetSRV(),
				lightCullData.pLightIndexGrid->GetSRV(),
				pSourceVolume->GetSRV(),
				});

			context.Dispatch(
				ComputeUtils::GetNumThreadGroups(
					pDestinationVolume->GetWidth(), 8,
					pDestinationVolume->GetHeight(), 8,
					pDestinationVolume->GetDepth(), 4)
			);
		});

	RGPassBuilder accumulateFog = graph.AddPass("Accumulate Volume Fog");
	accumulateFog.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
		{
			context.InsertResourceBarrier(pDestinationVolume, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(fogData.pFinalVolumeFog, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			context.SetComputeRootSignature(m_pVolumetricLightingRS);
			context.SetPipelineState(m_pAccumulateVolumeLightPSO);

			context.SetRootCBV(0, constantBuffer);
			context.SetRootCBV(1, GetViewUniforms(view));
			context.BindResources(2, fogData.pFinalVolumeFog->GetUAV());
			context.BindResources(3, {
				lightCullData.pLightGrid->GetSRV(),
				lightCullData.pLightIndexGrid->GetSRV(),
				pDestinationVolume->GetSRV(),
				});

			context.Dispatch(
				ComputeUtils::GetNumThreadGroups(
					pDestinationVolume->GetWidth(), 8,
					pDestinationVolume->GetHeight(), 8));
		});
}

void ClusteredForward::RenderBasePass(RGGraph& graph, const SceneView& view, const SceneTextures& sceneTextures, const ClusteredLightCullData& lightCullData, Texture* pFogTexture)
{
	static bool useMeshShader = false;
	if (ImGui::Begin("Parameters"))
	{
		if (ImGui::CollapsingHeader("Base Pass"))
		{
			if (ImGui::Checkbox("Mesh Shader", &useMeshShader))
			{
				useMeshShader = m_pMeshShaderDiffusePSO ? useMeshShader : false;
			}
		}
	}
	ImGui::End();

	RGPassBuilder basePass = graph.AddPass("Base Pass");
	basePass.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
		{
			context.InsertResourceBarrier(lightCullData.pLightGrid, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(lightCullData.pLightIndexGrid, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(sceneTextures.pAmbientOcclusion, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(sceneTextures.pPreviousColorTarget, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(pFogTexture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

			context.InsertResourceBarrier(sceneTextures.pDepth, D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(sceneTextures.pColorTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
			context.InsertResourceBarrier(sceneTextures.pNormalsTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
			context.InsertResourceBarrier(sceneTextures.pRoughnessTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);

			struct
			{
				IntVector4 ClusterDimensions;
				IntVector2 ClusterSize;
				Vector2 LightGridParams;
			} frameData;

			frameData.ClusterDimensions = lightCullData.ClusterCount;
			frameData.ClusterSize = IntVector2(gLightClusterTexelSize, gLightClusterTexelSize);
			frameData.LightGridParams = lightCullData.LightGridParams;

			RenderPassInfo renderPass;
			renderPass.DepthStencilTarget.Access = RenderPassAccess::Load_Store;
			renderPass.DepthStencilTarget.StencilAccess = RenderPassAccess::DontCare_DontCare;
			renderPass.DepthStencilTarget.Target = sceneTextures.pDepth;
			renderPass.DepthStencilTarget.Write = false;
			renderPass.RenderTargetCount = 3;
			renderPass.RenderTargets[0].Access = RenderPassAccess::DontCare_Store;
			renderPass.RenderTargets[0].Target = sceneTextures.pColorTarget;
			renderPass.RenderTargets[1].Access = RenderPassAccess::DontCare_Store;
			renderPass.RenderTargets[1].Target = sceneTextures.pNormalsTarget;
			renderPass.RenderTargets[2].Access = RenderPassAccess::DontCare_Store;
			renderPass.RenderTargets[2].Target = sceneTextures.pRoughnessTarget;
			context.BeginRenderPass(renderPass);

			context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context.SetGraphicsRootSignature(m_pDiffuseRS);

			context.SetRootCBV(1, frameData);

			context.SetRootCBV(2, GetViewUniforms(view, sceneTextures.pColorTarget));

			context.BindResources(3, {
				sceneTextures.pAmbientOcclusion->GetSRV(),
				sceneTextures.pDepth->GetSRV(),
				sceneTextures.pPreviousColorTarget->GetSRV(),
				pFogTexture->GetSRV(),
				lightCullData.pLightGrid->GetSRV(),
				lightCullData.pLightIndexGrid->GetSRV(),
				});

			{
				GPU_PROFILE_SCOPE("Opaque", &context);
				context.SetPipelineState(useMeshShader ? m_pMeshShaderDiffusePSO : m_pDiffusePSO);
				DrawScene(context, view, Batch::Blending::Opaque);
			}
			{
				GPU_PROFILE_SCOPE("Opaque - Masked", &context);
				context.SetPipelineState(useMeshShader ? m_pMeshShaderDiffuseMaskedPSO : m_pDiffuseMaskedPSO);
				DrawScene(context, view, Batch::Blending::AlphaMask);

			}
			{
				GPU_PROFILE_SCOPE("Transparant", &context);
				context.SetPipelineState(useMeshShader ? m_pMeshShaderDiffuseTransparancyPSO : m_pDiffuseTransparancyPSO);
				DrawScene(context, view, Batch::Blending::AlphaBlend);
			}

			context.EndRenderPass();
		});
}

void ClusteredForward::VisualizeLightDensity(RGGraph& graph, const SceneView& view, const SceneTextures& sceneTextures)
{
	const TextureDesc desc = sceneTextures.pColorTarget->GetDesc();
	if (!m_pVisualizationIntermediateTexture || m_pVisualizationIntermediateTexture->GetDesc() != desc)
	{
		m_pVisualizationIntermediateTexture = m_pDevice->CreateTexture(desc, "LightDensity Debug Texture");
	}

	Vector2 lightGridParams = ComputeVolumeGridParams(view.View.NearPlane, view.View.FarPlane, gLightClustersNumZ);

	RGPassBuilder basePass = graph.AddPass("Visualize Light Density");
	basePass.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
		{
			context.InsertResourceBarrier(sceneTextures.pColorTarget, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(sceneTextures.pDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_LightCullData.pLightGrid, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pVisualizationIntermediateTexture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			struct
			{
				IntVector2 ClusterDimensions;
				IntVector2 ClusterSize;
				Vector2 LightGridParams;
			} constantBuffer;

			constantBuffer.ClusterDimensions = IntVector2(m_LightCullData.ClusterCount.x, m_LightCullData.ClusterCount.y);
			constantBuffer.ClusterSize = IntVector2(gLightClusterTexelSize, gLightClusterTexelSize);
			constantBuffer.LightGridParams = lightGridParams;

			context.SetPipelineState(m_pVisualizeLightsPSO);
			context.SetComputeRootSignature(m_pVisualizeLightsRS);
			context.SetRootCBV(0, constantBuffer);
			context.SetRootCBV(1, GetViewUniforms(view, sceneTextures.pColorTarget));

			context.BindResources(2, {
				sceneTextures.pColorTarget->GetSRV(),
				sceneTextures.pDepth->GetSRV(),
				m_LightCullData.pLightGrid->GetSRV(),
				});
			context.BindResources(3, m_pVisualizationIntermediateTexture->GetUAV());

			context.Dispatch(ComputeUtils::GetNumThreadGroups(sceneTextures.pColorTarget->GetWidth(), 16, sceneTextures.pColorTarget->GetHeight(), 16));
			context.InsertUavBarrier();

			context.CopyTexture(m_pVisualizationIntermediateTexture, sceneTextures.pColorTarget);
		});
}
