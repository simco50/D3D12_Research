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
	RGBuffer* pLightGrid = nullptr;
	Vector2 LightGridParams;
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

void ClusteredForward::Execute(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures)
{
	ComputeLightCulling(graph, pView, m_LightCullData);

	RGTexture* pFogVolume = nullptr;
	if (Tweakables::g_VolumetricFog)
	{
		pFogVolume = RenderVolumetricFog(graph, pView, m_LightCullData, m_VolumetricFogData);
	}

	RenderBasePass(graph, pView, sceneTextures, m_LightCullData, pFogVolume);

	if (g_VisualizeClusters)
	{
		VisualizeClusters(graph, pView, sceneTextures, m_LightCullData);
	}
	else
	{
		m_LightCullData.DirtyDebugData = true;
	}
}

void ClusteredForward::ComputeLightCulling(RGGraph& graph, const SceneView* pView, ClusteredLightCullData& cullData)
{
	RG_GRAPH_SCOPE("Light Culling", graph);

	cullData.ClusterCount.x = Math::DivideAndRoundUp(pView->GetDimensions().x, gLightClusterTexelSize);
	cullData.ClusterCount.y = Math::DivideAndRoundUp(pView->GetDimensions().y, gLightClusterTexelSize);
	cullData.ClusterCount.z = gLightClustersNumZ;
	float nearZ = pView->View.NearPlane;
	float farZ = pView->View.FarPlane;
	float n = Math::Min(nearZ, farZ);
	float f = Math::Max(nearZ, farZ);
	cullData.LightGridParams.x = (float)gLightClustersNumZ / log(f / n);
	cullData.LightGridParams.y = ((float)gLightClustersNumZ * log(n)) / log(f / n);

	uint32 totalClusterCount = cullData.ClusterCount.x * cullData.ClusterCount.y * cullData.ClusterCount.z;

	cullData.pAABBs = graph.CreateBuffer("Cluster AABBs", BufferDesc::CreateStructured(totalClusterCount, sizeof(Vector4) * 2));

	graph.AddPass("Cluster AABBs", RGPassFlag::Compute)
		.Write(cullData.pAABBs)
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
				context.SetRootCBV(1, Renderer::GetViewUniforms(pView));
				context.BindResources(2, cullData.pAABBs->Get()->GetUAV());

				//Cluster count in z is 32 so fits nicely in a wavefront on Nvidia so make groupsize in shader 32
				constexpr uint32 threadGroupSize = 32;
				context.Dispatch(
					ComputeUtils::GetNumThreadGroups(
						cullData.ClusterCount.x, 1,
						cullData.ClusterCount.y, 1,
						cullData.ClusterCount.z, threadGroupSize)
				);
			});

	cullData.pLightIndexGrid = graph.CreateBuffer("Light Index Grid", BufferDesc::CreateStructured(gMaxLightsPerCluster * totalClusterCount, sizeof(uint32)));
	// LightGrid: x : Offset | y : Count
	cullData.pLightGrid = graph.CreateBuffer("Light Grid", BufferDesc::CreateStructured(2 * totalClusterCount, sizeof(uint32), BufferFlag::NoBindless));

	graph.AddPass("Cull Lights", RGPassFlag::Compute)
		.Read(cullData.pAABBs)
		.Write({ cullData.pLightGrid, cullData.pLightIndexGrid })
		.Bind([=](CommandContext& context, const RGPassResources& resources)
			{
				context.SetPipelineState(m_pLightCullingPSO);
				context.SetComputeRootSignature(m_pLightCullingRS);

				// Clear the light grid because we're accumulating the light count in the shader
				Buffer* pLightGrid = cullData.pLightGrid->Get();
				//#todo: adhoc UAV creation
				context.ClearUavUInt(pLightGrid, m_pDevice->CreateUAV(pLightGrid, BufferUAVDesc::CreateRaw()));

				struct
				{
					IntVector3 ClusterDimensions;
				} constantBuffer;

				constantBuffer.ClusterDimensions = cullData.ClusterCount;

				context.SetRootCBV(0, constantBuffer);

				context.SetRootCBV(1, Renderer::GetViewUniforms(pView));
				context.BindResources(2, {
					cullData.pLightIndexGrid->Get()->GetUAV(),
					cullData.pLightGrid->Get()->GetUAV(),
					});
				context.BindResources(3, cullData.pAABBs->Get()->GetSRV());

				context.Dispatch(
					ComputeUtils::GetNumThreadGroups(
						cullData.ClusterCount.x, 4,
						cullData.ClusterCount.y, 4,
						cullData.ClusterCount.z, 4)
				);
			});

	CullBlackboardData& blackboardData = graph.Blackboard.Add<CullBlackboardData>();
	blackboardData.pLightGrid = cullData.pLightGrid;
	blackboardData.LightGridParams = cullData.LightGridParams;
}

void ClusteredForward::VisualizeClusters(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures, ClusteredLightCullData& cullData)
{
	uint32 totalClusterCount = cullData.ClusterCount.x * cullData.ClusterCount.y * cullData.ClusterCount.z;
	RGBuffer* pDebugLightGrid = RGUtils::CreatePersistentBuffer(graph, "Debug Light Grid", BufferDesc::CreateStructured(2 * totalClusterCount, sizeof(uint32)), &cullData.pDebugLightGrid, true);

	if (cullData.DirtyDebugData)
	{
		RGUtils::AddCopyPass(graph, cullData.pLightGrid, pDebugLightGrid);
		cullData.DebugClustersViewMatrix = pView->View.ViewInverse;
		cullData.DirtyDebugData = false;
	}

	graph.AddPass("Visualize Clusters", RGPassFlag::Raster | RGPassFlag::AutoRenderPass)
		.Read({ pDebugLightGrid, cullData.pAABBs })
		.RenderTarget(sceneTextures.pColorTarget, RenderTargetLoadAction::Load)
		.DepthStencil(sceneTextures.pDepth, RenderTargetLoadAction::Load, false)
		.Bind([=](CommandContext& context, const RGPassResources& resources)
			{
				context.SetPipelineState(m_pVisualizeLightClustersPSO);
				context.SetGraphicsRootSignature(m_pVisualizeLightClustersRS);
				context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);

				ShaderInterop::ViewUniforms viewData = Renderer::GetViewUniforms(pView, sceneTextures.pColorTarget->Get());
				viewData.Projection = cullData.DebugClustersViewMatrix * pView->View.ViewProjection;
				context.SetRootCBV(0, viewData);
				context.BindResources(1, {
					cullData.pAABBs->Get()->GetSRV(),
					pDebugLightGrid->Get()->GetSRV(),
				m_pHeatMapTexture->GetSRV(),
					});
				context.Draw(0, cullData.ClusterCount.x * cullData.ClusterCount.y * cullData.ClusterCount.z);
			});
}

RGTexture* ClusteredForward::RenderVolumetricFog(RGGraph& graph, const SceneView* pView, const ClusteredLightCullData& lightCullData, VolumetricFogData& fogData)
{
	RG_GRAPH_SCOPE("Volumetric Lighting", graph);

	TextureDesc volumeDesc = TextureDesc::Create3D(
		Math::DivideAndRoundUp((uint32)pView->GetDimensions().x, gVolumetricFroxelTexelSize),
		Math::DivideAndRoundUp((uint32)pView->GetDimensions().y, gVolumetricFroxelTexelSize),
		gVolumetricNumZSlices,
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		TextureFlag::ShaderResource | TextureFlag::UnorderedAccess);

	RGTexture* pSourceVolume = RGUtils::CreatePersistentTexture(graph, "Fog History", volumeDesc, &fogData.pFogHistory, false);
	RGTexture* pTargetVolume = graph.CreateTexture("Fog Target", volumeDesc);
	RGTexture* pFinalVolumeFog = graph.CreateTexture("Volumetric Fog", volumeDesc);
	graph.ExportTexture(pTargetVolume, &fogData.pFogHistory);

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
	constantBuffer.Jitter = halton[pView->FrameIndex & 31];
	constantBuffer.LightClusterSizeFactor = (float)gVolumetricFroxelTexelSize / gLightClusterTexelSize;
	constantBuffer.LightGridParams = lightCullData.LightGridParams;
	constantBuffer.LightClusterDimensions = IntVector2(lightCullData.ClusterCount.x, lightCullData.ClusterCount.y);

	graph.AddPass("Inject Volume Lights", RGPassFlag::Compute)
		.Read({ pSourceVolume, lightCullData.pLightGrid, lightCullData.pLightIndexGrid })
		.Write(pTargetVolume)
		.Bind([=](CommandContext& context, const RGPassResources& resources)
			{
				Texture* pTarget = pTargetVolume->Get();

				context.SetComputeRootSignature(m_pVolumetricLightingRS);
				context.SetPipelineState(m_pInjectVolumeLightPSO);

				context.SetRootCBV(0, constantBuffer);
				context.SetRootCBV(1, Renderer::GetViewUniforms(pView));
				context.BindResources(2, pTarget->GetUAV());
				context.BindResources(3, {
					lightCullData.pLightGrid->Get()->GetSRV(),
					lightCullData.pLightIndexGrid->Get()->GetSRV(),
					pSourceVolume->Get()->GetSRV(),
					});

				context.Dispatch(
					ComputeUtils::GetNumThreadGroups(
						pTarget->GetWidth(), 8,
						pTarget->GetHeight(), 8,
						pTarget->GetDepth(), 4)
				);
			});

	graph.AddPass("Accumulate Volume Fog", RGPassFlag::Compute)
		.Read({ pTargetVolume, lightCullData.pLightGrid, lightCullData.pLightIndexGrid })
		.Write(pFinalVolumeFog)
		.Bind([=](CommandContext& context, const RGPassResources& resources)
			{
				Texture* pFinalFog = pFinalVolumeFog->Get();

				context.SetComputeRootSignature(m_pVolumetricLightingRS);
				context.SetPipelineState(m_pAccumulateVolumeLightPSO);

				context.SetRootCBV(0, constantBuffer);
				context.SetRootCBV(1, Renderer::GetViewUniforms(pView));
				context.BindResources(2, pFinalFog->GetUAV());
				context.BindResources(3, {
					lightCullData.pLightGrid->Get()->GetSRV(),
					lightCullData.pLightIndexGrid->Get()->GetSRV(),
					pTargetVolume->Get()->GetSRV(),
					});

				context.Dispatch(
					ComputeUtils::GetNumThreadGroups(
						pFinalFog->GetWidth(), 8,
						pFinalFog->GetHeight(), 8));
			});
	return pFinalVolumeFog;
}

void ClusteredForward::RenderBasePass(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures, const ClusteredLightCullData& lightCullData, RGTexture* pFogTexture)
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

	if (!pFogTexture)
	{
		pFogTexture = graph.ImportTexture(GraphicsCommon::GetDefaultTexture(DefaultTexture::Black3D));
	}

	graph.AddPass("Base Pass", RGPassFlag::Raster | RGPassFlag::AutoRenderPass)
		.Read({ sceneTextures.pAmbientOcclusion, sceneTextures.pPreviousColor, pFogTexture, sceneTextures.pDepth })
		.Read({ lightCullData.pLightGrid, lightCullData.pLightIndexGrid })
		.DepthStencil(sceneTextures.pDepth, RenderTargetLoadAction::Load, false)
		.RenderTarget(sceneTextures.pColorTarget, RenderTargetLoadAction::DontCare)
		.RenderTarget(sceneTextures.pNormals, RenderTargetLoadAction::DontCare)
		.RenderTarget(sceneTextures.pRoughness, RenderTargetLoadAction::DontCare)
		.Bind([=](CommandContext& context, const RGPassResources& resources)
			{
				context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				context.SetGraphicsRootSignature(m_pDiffuseRS);

				struct
				{
					IntVector4 ClusterDimensions;
					IntVector2 ClusterSize;
					Vector2 LightGridParams;
				} frameData;

				frameData.ClusterDimensions = lightCullData.ClusterCount;
				frameData.ClusterSize = IntVector2(gLightClusterTexelSize, gLightClusterTexelSize);
				frameData.LightGridParams = lightCullData.LightGridParams;

				context.SetRootCBV(1, frameData);
				context.SetRootCBV(2, Renderer::GetViewUniforms(pView, sceneTextures.pColorTarget->Get()));

				context.BindResources(3, {
					sceneTextures.pAmbientOcclusion->Get()->GetSRV(),
					sceneTextures.pDepth->Get()->GetSRV(),
					sceneTextures.pPreviousColor->Get()->GetSRV(),
					pFogTexture->Get()->GetSRV(),
					lightCullData.pLightGrid->Get()->GetSRV(),
					lightCullData.pLightIndexGrid->Get()->GetSRV(),
					});

				{
					GPU_PROFILE_SCOPE("Opaque", &context);
					context.SetPipelineState(useMeshShader ? m_pMeshShaderDiffusePSO : m_pDiffusePSO);
					Renderer::DrawScene(context, pView, Batch::Blending::Opaque);
				}
				{
					GPU_PROFILE_SCOPE("Opaque - Masked", &context);
					context.SetPipelineState(useMeshShader ? m_pMeshShaderDiffuseMaskedPSO : m_pDiffuseMaskedPSO);
					Renderer::DrawScene(context, pView, Batch::Blending::AlphaMask);

				}
				{
					GPU_PROFILE_SCOPE("Transparant", &context);
					context.SetPipelineState(useMeshShader ? m_pMeshShaderDiffuseTransparancyPSO : m_pDiffuseTransparancyPSO);
					Renderer::DrawScene(context, pView, Batch::Blending::AlphaBlend);
				}
			});
}

void ClusteredForward::VisualizeLightDensity(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures)
{
	RGTexture* pVisualizationTarget = graph.CreateTexture("Scene Color", sceneTextures.pColorTarget->GetDesc());

	const CullBlackboardData& blackboardData = graph.Blackboard.Get<CullBlackboardData>();
	RGBuffer* pLightGrid = blackboardData.pLightGrid;
	Vector2 lightGridParams = blackboardData.LightGridParams;

	graph.AddPass("Visualize Light Density", RGPassFlag::Compute)
		.Read({ sceneTextures.pDepth, sceneTextures.pColorTarget, pLightGrid })
		.Write(pVisualizationTarget)
		.Bind([=](CommandContext& context, const RGPassResources& resources)
			{
				Texture* pTarget = pVisualizationTarget->Get();

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
				context.SetRootCBV(1, Renderer::GetViewUniforms(pView, pTarget));

				context.BindResources(2, {
					sceneTextures.pColorTarget->Get()->GetSRV(),
					sceneTextures.pDepth->Get()->GetSRV(),
					pLightGrid->Get()->GetSRV(),
					});
				context.BindResources(3, pTarget->GetUAV());

				context.Dispatch(ComputeUtils::GetNumThreadGroups(
					pTarget->GetWidth(), 16,
					pTarget->GetHeight(), 16));
			});

	sceneTextures.pColorTarget = pVisualizationTarget;
}
