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

struct CullBlackboardData
{
	RGResourceHandle LightGrid;
};
RG_BLACKBOARD_DATA(CullBlackboardData);

namespace Tweakables
{
	extern ConsoleVariable<bool> g_VolumetricFog;
}
bool g_VisualizeClusters = false;

ClusteredForward::ClusteredForward(GraphicsDevice* pDevice)
	: m_pDevice(pDevice)
{
	CommandContext* pContext = pDevice->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
	m_pHeatMapTexture = GraphicsCommon::CreateTextureFromFile(*pContext, "Resources/Textures/Heatmap.png", true, "Color Heatmap");
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

void ClusteredForward::Execute(RGGraph& graph, const SceneView& view, SceneTextures& sceneTextures)
{
	ComputeLightCulling(graph, view, m_LightCullData);

	RGResourceHandle fogVolume = graph.ImportTexture("Black", GraphicsCommon::GetDefaultTexture(DefaultTexture::Black3D));
	if (Tweakables::g_VolumetricFog)
	{
		fogVolume = RenderVolumetricFog(graph, view, m_LightCullData, m_VolumetricFogData);
	}

	RenderBasePass(graph, view, sceneTextures, m_LightCullData, fogVolume);

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

	resources.pDebugLightGrid = m_pDevice->CreateBuffer(BufferDesc::CreateStructured(2 * totalClusterCount, sizeof(uint32)), "Debug Light Grid");
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

void ClusteredForward::ComputeLightCulling(RGGraph& graph, const SceneView& view, ClusteredLightCullData& cullData)
{
	float nearZ = view.View.NearPlane;
	float farZ = view.View.FarPlane;
	cullData.LightGridParams = ComputeVolumeGridParams(nearZ, farZ, gLightClustersNumZ);

	cullData.AABBs = graph.ImportBuffer("Cluster AABBs", cullData.pAABBs);

	if (cullData.IsViewDirty)
	{
		cullData.IsViewDirty = false;

		graph.AddPass("Cluster AABBs", RGPassFlag::Compute)
			.Write(&cullData.AABBs)
			.Bind([=](CommandContext& context, const RGPassResources& resources)
			{
				context.SetPipelineState(m_pCreateAabbPSO);
				context.SetComputeRootSignature(m_pLightCullingRS);

				struct
				{
					IntVector4 ClusterDimensions;
					IntVector2 ClusterSize;
				} constantBuffer;

				constantBuffer.ClusterSize = IntVector2(gLightClusterTexelSize, gLightClusterTexelSize);
				constantBuffer.ClusterDimensions = IntVector4(cullData.ClusterCount.x, cullData.ClusterCount.y, cullData.ClusterCount.z, 0);

				context.SetRootCBV(0, constantBuffer);
				context.SetRootCBV(1, Renderer::GetViewUniforms(view));
				context.BindResources(2, resources.Get<Buffer>(cullData.AABBs)->GetUAV());

				//Cluster count in z is 32 so fits nicely in a wavefront on Nvidia so make groupsize in shader 32
				constexpr uint32 threadGroupSize = 32;
				context.Dispatch(
					ComputeUtils::GetNumThreadGroups(
						cullData.ClusterCount.x, 1,
						cullData.ClusterCount.y, 1,
						cullData.ClusterCount.z, threadGroupSize)
				);
			});
	}

	uint32 totalClusterCount = cullData.ClusterCount.x * cullData.ClusterCount.y * cullData.ClusterCount.z;
	cullData.LightIndexGrid = graph.CreateBuffer("Light Index Grid", BufferDesc::CreateStructured(gMaxLightsPerCluster * totalClusterCount, sizeof(uint32)));
	// LightGrid: x : Offset | y : Count
	cullData.LightGrid = graph.CreateBuffer("Light Grid", BufferDesc::CreateStructured(2 * totalClusterCount, sizeof(uint32), BufferFlag::NoBindless));

	graph.AddPass("Light Culling", RGPassFlag::Compute)
		.Read(cullData.AABBs)
		.Write({ &cullData.LightGrid, &cullData.LightIndexGrid })
		.Bind([=](CommandContext& context, const RGPassResources& resources)
		{
			context.SetPipelineState(m_pLightCullingPSO);
			context.SetComputeRootSignature(m_pLightCullingRS);

			// Clear the light grid because we're accumulating the light count in the shader
			Buffer* pLightGrid = resources.Get<Buffer>(cullData.LightGrid);
			//#todo: adhoc UAV creation
			context.ClearUavUInt(pLightGrid, m_pDevice->CreateUAV(pLightGrid, BufferUAVDesc::CreateRaw()));

			struct
			{
				IntVector3 ClusterDimensions;
			} constantBuffer;

			constantBuffer.ClusterDimensions = cullData.ClusterCount;

			context.SetRootCBV(0, constantBuffer);

			context.SetRootCBV(1, Renderer::GetViewUniforms(view));
			context.BindResources(2, {
				resources.Get<Buffer>(cullData.LightIndexGrid)->GetUAV(),
				resources.Get<Buffer>(cullData.LightGrid)->GetUAV(),
				});
			context.BindResources(3, resources.Get<Buffer>(cullData.AABBs)->GetSRV());

			context.Dispatch(
				ComputeUtils::GetNumThreadGroups(
					cullData.ClusterCount.x, 4,
					cullData.ClusterCount.y, 4,
					cullData.ClusterCount.z, 4)
			);
		});

	CullBlackboardData& backboardData = graph.Blackboard.Add<CullBlackboardData>();
	backboardData.LightGrid = cullData.LightGrid;
}

void ClusteredForward::VisualizeClusters(RGGraph& graph, const SceneView& view, SceneTextures& sceneTextures, ClusteredLightCullData& cullData)
{
	RGResourceHandle debugLightGrid = graph.ImportBuffer("Debug Light Grid", cullData.pDebugLightGrid);

	if (cullData.DirtyDebugData)
	{
		graph.AddCopyPass("Cache Debug Light Grid", cullData.LightGrid, debugLightGrid);
		cullData.DebugClustersViewMatrix = view.View.ViewInverse;
		cullData.DirtyDebugData = false;
	}

	graph.AddPass("Visualize Clusters", RGPassFlag::Raster)
		.Read(debugLightGrid)
		.RenderTarget(sceneTextures.ColorTarget, RenderPassAccess::Load_Store)
		.DepthStencil(sceneTextures.Depth, RenderPassAccess::Load_Store, false)
		.Bind([=](CommandContext& context, const RGPassResources& resources)
		{
			context.BeginRenderPass(resources.GetRenderPassInfo());

			context.SetPipelineState(m_pVisualizeLightClustersPSO);
			context.SetGraphicsRootSignature(m_pVisualizeLightClustersRS);
			context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);

			ShaderInterop::ViewUniforms viewData = Renderer::GetViewUniforms(view, resources.Get<Texture>(sceneTextures.ColorTarget));
			viewData.Projection = cullData.DebugClustersViewMatrix * view.View.ViewProjection;
			context.SetRootCBV(0, viewData);
			context.BindResources(1, {
				resources.Get<Buffer>(cullData.AABBs)->GetSRV(),
				resources.Get<Buffer>(debugLightGrid)->GetSRV(),
				m_pHeatMapTexture->GetSRV(),
				});
			context.Draw(0, cullData.ClusterCount.x * cullData.ClusterCount.y * cullData.ClusterCount.z);

			context.EndRenderPass();
		});
}

RGResourceHandle ClusteredForward::RenderVolumetricFog(RGGraph& graph, const SceneView& view, const ClusteredLightCullData& lightCullData, VolumetricFogData& fogData)
{
	RG_GRAPH_SCOPE("Volumetric Lighting", graph);

	RGResourceHandle sourceVolume = graph.ImportTexture("Fog Source", fogData.pLightScatteringVolume[view.FrameIndex % 2]);
	RGResourceHandle targetVolume = graph.ImportTexture("Fog Target", fogData.pLightScatteringVolume[(view.FrameIndex + 1) % 2]);
	TextureDesc volumeDesc = graph.GetDesc(sourceVolume);

	RGResourceHandle finalVolumeFog = graph.CreateTexture("Volumetric Fog", volumeDesc);

	struct
	{
		IntVector3 ClusterDimensions;
		float Jitter;
		Vector3 InvClusterDimensions;
		float LightClusterSizeFactor;
		Vector2 LightGridParams;
		IntVector2 LightClusterDimensions;
	} constantBuffer;

	constantBuffer.ClusterDimensions = IntVector3(volumeDesc.Width, volumeDesc.Height, volumeDesc.DepthOrArraySize);
	constantBuffer.InvClusterDimensions = Vector3(1.0f / volumeDesc.Width, 1.0f / volumeDesc.Height, 1.0f / volumeDesc.DepthOrArraySize);
	constexpr Math::HaltonSequence<32, 2> halton;
	constantBuffer.Jitter = halton[view.FrameIndex & 31];
	constantBuffer.LightClusterSizeFactor = (float)gVolumetricFroxelTexelSize / gLightClusterTexelSize;
	constantBuffer.LightGridParams = lightCullData.LightGridParams;
	constantBuffer.LightClusterDimensions = IntVector2(lightCullData.ClusterCount.x, lightCullData.ClusterCount.y);

	graph.AddPass("Inject Volume Lights", RGPassFlag::Compute)
		.Read({ sourceVolume, lightCullData.LightGrid, lightCullData.LightIndexGrid })
		.Write(&targetVolume)
		.Bind([=](CommandContext& context, const RGPassResources& resources)
			{
				Texture* pTargetVolume = resources.Get<Texture>(targetVolume);

				context.SetComputeRootSignature(m_pVolumetricLightingRS);
				context.SetPipelineState(m_pInjectVolumeLightPSO);

				context.SetRootCBV(0, constantBuffer);
				context.SetRootCBV(1, Renderer::GetViewUniforms(view));
				context.BindResources(2, pTargetVolume->GetUAV());
				context.BindResources(3, {
					resources.Get<Buffer>(lightCullData.LightGrid)->GetSRV(),
					resources.Get<Buffer>(lightCullData.LightIndexGrid)->GetSRV(),
					resources.Get<Texture>(sourceVolume)->GetSRV(),
					});

				context.Dispatch(
					ComputeUtils::GetNumThreadGroups(
						pTargetVolume->GetWidth(), 8,
						pTargetVolume->GetHeight(), 8,
						pTargetVolume->GetDepth(), 4)
				);
			});

	graph.AddPass("Accumulate Volume Fog", RGPassFlag::Compute)
		.Read({ targetVolume, lightCullData.LightGrid, lightCullData.LightIndexGrid })
		.Write(&finalVolumeFog)
		.Bind([=](CommandContext& context, const RGPassResources& resources)
			{
				Texture* pFinalFog = resources.Get<Texture>(finalVolumeFog);

				context.SetComputeRootSignature(m_pVolumetricLightingRS);
				context.SetPipelineState(m_pAccumulateVolumeLightPSO);

				context.SetRootCBV(0, constantBuffer);
				context.SetRootCBV(1, Renderer::GetViewUniforms(view));
				context.BindResources(2, pFinalFog->GetUAV());
				context.BindResources(3, {
					resources.Get<Buffer>(lightCullData.LightGrid)->GetSRV(),
					resources.Get<Buffer>(lightCullData.LightIndexGrid)->GetSRV(),
					resources.Get<Texture>(targetVolume)->GetSRV(),
					});

				context.Dispatch(
					ComputeUtils::GetNumThreadGroups(
						pFinalFog->GetWidth(), 8,
						pFinalFog->GetHeight(), 8));
			});
	return finalVolumeFog;
}

void ClusteredForward::RenderBasePass(RGGraph& graph, const SceneView& view, SceneTextures& sceneTextures, const ClusteredLightCullData& lightCullData, RGResourceHandle fogTexture)
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

	graph.AddPass("Base Pass", RGPassFlag::Raster)
		.Read({ sceneTextures.AmbientOcclusion, sceneTextures.PreviousColor, fogTexture })
		.Read({ lightCullData.LightGrid, lightCullData.LightIndexGrid })
		.DepthStencil(sceneTextures.Depth, RenderPassAccess::Load_Store, false)
		.RenderTarget(sceneTextures.ColorTarget, RenderPassAccess::DontCare_Store)
		.RenderTarget(sceneTextures.Normals, RenderPassAccess::DontCare_Store)
		.RenderTarget(sceneTextures.Roughness, RenderPassAccess::DontCare_Store)
		.Bind([=](CommandContext& context, const RGPassResources& resources)
			{
				struct
				{
					IntVector4 ClusterDimensions;
					IntVector2 ClusterSize;
					Vector2 LightGridParams;
				} frameData;

				frameData.ClusterDimensions = lightCullData.ClusterCount;
				frameData.ClusterSize = IntVector2(gLightClusterTexelSize, gLightClusterTexelSize);
				frameData.LightGridParams = lightCullData.LightGridParams;

				context.BeginRenderPass(resources.GetRenderPassInfo());

				context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				context.SetGraphicsRootSignature(m_pDiffuseRS);

				context.SetRootCBV(1, frameData);

				context.SetRootCBV(2, Renderer::GetViewUniforms(view, resources.Get<Texture>(sceneTextures.ColorTarget)));

				context.BindResources(3, {
					resources.Get<Texture>(sceneTextures.AmbientOcclusion)->GetSRV(),
					resources.Get<Texture>(sceneTextures.Depth)->GetSRV(),
					resources.Get<Texture>(sceneTextures.PreviousColor)->GetSRV(),
					resources.Get<Texture>(fogTexture)->GetSRV(),
					resources.Get<Buffer>(lightCullData.LightGrid)->GetSRV(),
					resources.Get<Buffer>(lightCullData.LightIndexGrid)->GetSRV(),
					});

				{
					GPU_PROFILE_SCOPE("Opaque", &context);
					context.SetPipelineState(useMeshShader ? m_pMeshShaderDiffusePSO : m_pDiffusePSO);
					Renderer::DrawScene(context, view, Batch::Blending::Opaque);
				}
				{
					GPU_PROFILE_SCOPE("Opaque - Masked", &context);
					context.SetPipelineState(useMeshShader ? m_pMeshShaderDiffuseMaskedPSO : m_pDiffuseMaskedPSO);
					Renderer::DrawScene(context, view, Batch::Blending::AlphaMask);

				}
				{
					GPU_PROFILE_SCOPE("Transparant", &context);
					context.SetPipelineState(useMeshShader ? m_pMeshShaderDiffuseTransparancyPSO : m_pDiffuseTransparancyPSO);
					Renderer::DrawScene(context, view, Batch::Blending::AlphaBlend);
				}

				context.EndRenderPass();
			});
}

void ClusteredForward::VisualizeLightDensity(RGGraph& graph, const SceneView& view, SceneTextures& sceneTextures)
{
	RGResourceHandle visualizationIntermediate = graph.CreateTexture("Cached Scene Color", graph.GetDesc(sceneTextures.ColorTarget));

	Vector2 lightGridParams = ComputeVolumeGridParams(view.View.NearPlane, view.View.FarPlane, gLightClustersNumZ);

	const CullBlackboardData& blackboardData = graph.Blackboard.Get<CullBlackboardData>();
	RGResourceHandle lightGrid = blackboardData.LightGrid;

	graph.AddCopyPass("Cache Scene Color", sceneTextures.ColorTarget, visualizationIntermediate);

	graph.AddPass("Visualize Light Density", RGPassFlag::Compute)
		.Read({ sceneTextures.Depth, visualizationIntermediate, lightGrid })
		.Write(&sceneTextures.ColorTarget)
		.Bind([=](CommandContext& context, const RGPassResources& resources)
			{
				Texture* pTarget = resources.Get<Texture>(sceneTextures.ColorTarget);

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
				context.SetRootCBV(1, Renderer::GetViewUniforms(view, pTarget));

				context.BindResources(2, {
					resources.Get<Texture>(visualizationIntermediate)->GetSRV(),
					resources.Get<Texture>(sceneTextures.Depth)->GetSRV(),
					resources.Get<Buffer>(lightGrid)->GetSRV(),
					});
				context.BindResources(3, pTarget->GetUAV());

				context.Dispatch(ComputeUtils::GetNumThreadGroups(
					pTarget->GetWidth(), 16,
					pTarget->GetHeight(), 16));
			});
}
