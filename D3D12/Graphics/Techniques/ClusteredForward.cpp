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
	SetupPipelines();

	CommandContext* pContext = pDevice->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
	m_pHeatMapTexture = new Texture(pDevice, "Heatmap Texture");
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

void ClusteredForward::Execute(RGGraph& graph, const SceneView& resources, const SceneTextures& parameters)
{
	RG_GRAPH_SCOPE("Clustered Lighting", graph);

	ClusteredLightCullData lightCullData;
	lightCullData.ClusterCount = IntVector3(m_ClusterCountX, m_ClusterCountY, gLightClustersNumZ);
	lightCullData.pAABBs = m_pAABBs;
	lightCullData.pLightIndexGrid = m_pLightIndexGrid;
	lightCullData.pLightGrid = m_pLightGrid;
	lightCullData.pLightGridRawUAV = m_pLightGridRawUAV;
	ComputeLightCulling(graph, resources, lightCullData);

	Texture* pFogVolume = GraphicsCommon::GetDefaultTexture(DefaultTexture::Black3D);

	if (Tweakables::g_VolumetricFog)
	{
		VolumetricFogData fogData;
		fogData.pFinalVolumeFog = m_pFinalVolumeFog;
		fogData.pLightScatteringVolume[0] = m_pLightScatteringVolume[0];
		fogData.pLightScatteringVolume[1] = m_pLightScatteringVolume[1];
		RenderVolumetricFog(graph, resources, lightCullData, fogData);
		pFogVolume = fogData.pFinalVolumeFog;
	}

	RenderBasePass(graph, resources, parameters, lightCullData, pFogVolume);
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

void ClusteredForward::ComputeLightCulling(RGGraph& graph, const SceneView& scene, ClusteredLightCullData& resources)
{
	Vector2 screenDimensions(scene.View.Viewport.GetWidth(), scene.View.Viewport.GetHeight());
	float nearZ = scene.View.NearPlane;
	float farZ = scene.View.FarPlane;
	resources.LightGridParams = ComputeVolumeGridParams(nearZ, farZ, gLightClustersNumZ);

	if (m_ViewportDirty)
	{
		RGPassBuilder calculateAabbs = graph.AddPass("Cluster AABBs");
		calculateAabbs.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
			{
				context.InsertResourceBarrier(resources.pAABBs, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

				context.SetPipelineState(m_pCreateAabbPSO);
				context.SetComputeRootSignature(m_pCreateAabbRS);

				struct ConstantBuffer
				{
					IntVector4 ClusterDimensions;
					IntVector2 ClusterSize;
				} constantBuffer;

				constantBuffer.ClusterSize = IntVector2(gLightClusterTexelSize, gLightClusterTexelSize);
				constantBuffer.ClusterDimensions = IntVector4(resources.ClusterCount.x, resources.ClusterCount.y, resources.ClusterCount.z, 0);

				context.SetRootCBV(0, constantBuffer);
				context.SetRootCBV(1, GetViewUniforms(scene));
				context.BindResource(2, 0, resources.pAABBs->GetUAV());

				//Cluster count in z is 32 so fits nicely in a wavefront on Nvidia so make groupsize in shader 32
				constexpr uint32 threadGroupSize = 32;
				context.Dispatch(
					ComputeUtils::GetNumThreadGroups(
						resources.ClusterCount.x, 1,
						resources.ClusterCount.y, 1,
						resources.ClusterCount.z, threadGroupSize)
				);
			});
		m_ViewportDirty = false;
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

			struct ConstantBuffer
			{
				IntVector3 ClusterDimensions;
			} constantBuffer{};

			constantBuffer.ClusterDimensions = resources.ClusterCount;

			context.SetRootCBV(0, constantBuffer);

			context.SetRootCBV(1, GetViewUniforms(scene));

			context.BindResource(2, 0, resources.pAABBs->GetSRV());

			context.BindResource(3, 0, resources.pLightIndexGrid->GetUAV());
			context.BindResource(3, 1, resources.pLightGrid->GetUAV());

			constexpr uint32 threadGroupSize = 4;
			context.Dispatch(
				ComputeUtils::GetNumThreadGroups(
					resources.ClusterCount.x, threadGroupSize,
					resources.ClusterCount.y, threadGroupSize,
					resources.ClusterCount.z, threadGroupSize)
			);
		});
}

void ClusteredForward::RenderVolumetricFog(RGGraph& graph, const SceneView& scene, const ClusteredLightCullData& lightCullData, VolumetricFogData& fogData)
{
	RG_GRAPH_SCOPE("Volumetric Lighting", graph);

	Texture* pSourceVolume = fogData.pLightScatteringVolume[scene.FrameIndex % 2];
	Texture* pDestinationVolume = fogData.pLightScatteringVolume[(scene.FrameIndex + 1) % 2];

	struct ConstantBuffer
	{
		IntVector3 ClusterDimensions;
		float Jitter;
		Vector3 InvClusterDimensions;
		float LightClusterSizeFactor;
		Vector2 LightGridParams;
		IntVector2 LightClusterDimensions;
	} constantBuffer{};

	constantBuffer.ClusterDimensions = IntVector3(pDestinationVolume->GetWidth(), pDestinationVolume->GetHeight(), pDestinationVolume->GetDepth());
	constantBuffer.InvClusterDimensions = Vector3(1.0f / pDestinationVolume->GetWidth(), 1.0f / pDestinationVolume->GetHeight(), 1.0f / pDestinationVolume->GetDepth());
	constexpr Math::HaltonSequence<32, 2> halton;
	constantBuffer.Jitter = halton[scene.FrameIndex & 31];
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

			D3D12_CPU_DESCRIPTOR_HANDLE srvs[] = {
				lightCullData.pLightGrid->GetSRV()->GetDescriptor(),
				lightCullData.pLightIndexGrid->GetSRV()->GetDescriptor(),
				pSourceVolume->GetSRV()->GetDescriptor(),
			};

			context.SetRootCBV(0, constantBuffer);
			context.SetRootCBV(1, GetViewUniforms(scene));
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
			context.InsertResourceBarrier(fogData.pFinalVolumeFog, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			context.SetComputeRootSignature(m_pVolumetricLightingRS);
			context.SetPipelineState(m_pAccumulateVolumeLightPSO);

			//float values[] = { 0,0,0,0 };
			//context.ClearUavFloat(m_pFinalVolumeFog, m_pFinalVolumeFog->GetUAV(), values);

			D3D12_CPU_DESCRIPTOR_HANDLE srvs[] = {
				lightCullData.pLightGrid->GetSRV()->GetDescriptor(),
				lightCullData.pLightIndexGrid->GetSRV()->GetDescriptor(),
				pDestinationVolume->GetSRV()->GetDescriptor(),
			};

			context.SetRootCBV(0, constantBuffer);
			context.SetRootCBV(1, GetViewUniforms(scene));
			context.BindResource(2, 0, fogData.pFinalVolumeFog->GetUAV());
			context.BindResources(3, 0, srvs, ARRAYSIZE(srvs));

			constexpr uint32 threadGroupSize = 8;

			context.Dispatch(
				ComputeUtils::GetNumThreadGroups(
					pDestinationVolume->GetWidth(), threadGroupSize,
					pDestinationVolume->GetHeight(), threadGroupSize));
		});
}

void ClusteredForward::RenderBasePass(RGGraph& graph, const SceneView& resources, const SceneTextures& parameters, const ClusteredLightCullData& lightCullData, Texture* pFogTexture)
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
			struct PerFrameData
			{
				IntVector4 ClusterDimensions;
				IntVector2 ClusterSize;
				Vector2 LightGridParams;
			} frameData{};

			frameData.ClusterDimensions = lightCullData.ClusterCount;
			frameData.ClusterSize = IntVector2(gLightClusterTexelSize, gLightClusterTexelSize);
			frameData.LightGridParams = lightCullData.LightGridParams;

			context.InsertResourceBarrier(lightCullData.pLightGrid, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(lightCullData.pLightIndexGrid, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(parameters.pAmbientOcclusion, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(parameters.pPreviousColorTarget, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(pFogTexture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

			context.InsertResourceBarrier(parameters.pDepth, D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(parameters.pColorTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
			context.InsertResourceBarrier(parameters.pNormalsTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);

			RenderPassInfo renderPass;
			renderPass.DepthStencilTarget.Access = RenderPassAccess::Load_Store;
			renderPass.DepthStencilTarget.StencilAccess = RenderPassAccess::DontCare_DontCare;
			renderPass.DepthStencilTarget.Target = parameters.pDepth;
			renderPass.DepthStencilTarget.Write = false;
			renderPass.RenderTargetCount = 2;
			renderPass.RenderTargets[0].Access = RenderPassAccess::Clear_Store;
			renderPass.RenderTargets[0].Target = parameters.pColorTarget;
			renderPass.RenderTargets[1].Access = RenderPassAccess::Clear_Store;
			renderPass.RenderTargets[1].Target = parameters.pNormalsTarget;
			context.BeginRenderPass(renderPass);

			context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context.SetGraphicsRootSignature(m_pDiffuseRS);

			context.SetRootCBV(1, frameData);

			context.SetRootCBV(2, GetViewUniforms(resources, parameters.pColorTarget));

			D3D12_CPU_DESCRIPTOR_HANDLE srvs[] = {
				parameters.pAmbientOcclusion->GetSRV()->GetDescriptor(),
				parameters.pDepth->GetSRV()->GetDescriptor(),
				parameters.pPreviousColorTarget->GetSRV()->GetDescriptor(),
				pFogTexture->GetSRV()->GetDescriptor(),
				lightCullData.pLightGrid->GetSRV()->GetDescriptor(),
				lightCullData.pLightIndexGrid->GetSRV()->GetDescriptor(),
			};
			context.BindResources(3, 0, srvs, ARRAYSIZE(srvs));

			{
				GPU_PROFILE_SCOPE("Opaque", &context);
				context.SetPipelineState(useMeshShader ? m_pMeshShaderDiffusePSO : m_pDiffusePSO);
				DrawScene(context, resources, Batch::Blending::Opaque);
			}
			{
				GPU_PROFILE_SCOPE("Opaque - Masked", &context);
				context.SetPipelineState(useMeshShader ? m_pMeshShaderDiffuseMaskedPSO : m_pDiffuseMaskedPSO);
				DrawScene(context, resources, Batch::Blending::AlphaMask);

			}
			{
				GPU_PROFILE_SCOPE("Transparant", &context);
				context.SetPipelineState(useMeshShader ? m_pMeshShaderDiffuseTransparancyPSO : m_pDiffuseTransparancyPSO);
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
					context.CopyTexture(m_pLightGrid, m_pDebugLightGrid);
					m_DebugClustersViewMatrix = resources.View.View;
					m_DebugClustersViewMatrix.Invert(m_DebugClustersViewMatrix);
					m_DidCopyDebugClusterData = true;
				}

				context.BeginRenderPass(RenderPassInfo(parameters.pColorTarget, RenderPassAccess::Load_Store, parameters.pDepth, RenderPassAccess::Load_Store, false));

				context.SetPipelineState(m_pVisualizeLightClustersPSO);
				context.SetGraphicsRootSignature(m_pVisualizeLightClustersRS);
				context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);

				ShaderInterop::ViewUniforms view = GetViewUniforms(resources, parameters.pColorTarget);
				view.Projection = m_DebugClustersViewMatrix * resources.View.ViewProjection;
				context.SetRootCBV(0, view);

				D3D12_CPU_DESCRIPTOR_HANDLE srvs[] = {
					lightCullData.pAABBs->GetSRV()->GetDescriptor(),
					m_pDebugLightGrid->GetSRV()->GetDescriptor(),
					m_pHeatMapTexture->GetSRV()->GetDescriptor(),
				};
				context.BindResources(1, 0, srvs, ARRAYSIZE(srvs));

				context.Draw(0, lightCullData.ClusterCount.x * lightCullData.ClusterCount.y * lightCullData.ClusterCount.z);

				context.EndRenderPass();
			});
	}
	else
	{
		m_DidCopyDebugClusterData = false;
	}
}

void ClusteredForward::VisualizeLightDensity(RGGraph& graph, const SceneView& resources, Texture* pTarget, Texture* pDepth)
{
	if (!m_pVisualizationIntermediateTexture || m_pVisualizationIntermediateTexture->GetDesc() != pTarget->GetDesc())
	{
		m_pVisualizationIntermediateTexture = m_pDevice->CreateTexture(pTarget->GetDesc(), "Light Density Debug Texture");
	}

	Vector2 screenDimensions((float)pTarget->GetWidth(), (float)pTarget->GetHeight());
	float nearZ = resources.View.NearPlane;
	float farZ = resources.View.FarPlane;
	Vector2 lightGridParams = ComputeVolumeGridParams(nearZ, farZ, gLightClustersNumZ);

	RGPassBuilder basePass = graph.AddPass("Visualize Light Density");
	basePass.Bind([=](CommandContext& context, const RGPassResources& /*passResources*/)
		{
			struct ConstantBuffer
			{
				IntVector2 ClusterDimensions;
				IntVector2 ClusterSize;
				Vector2 LightGridParams;
			} constantBuffer;

			constantBuffer.ClusterDimensions = IntVector2(m_ClusterCountX, m_ClusterCountY);
			constantBuffer.ClusterSize = IntVector2(gLightClusterTexelSize, gLightClusterTexelSize);
			constantBuffer.LightGridParams = lightGridParams;

			context.SetPipelineState(m_pVisualizeLightsPSO);
			context.SetComputeRootSignature(m_pVisualizeLightsRS);

			context.InsertResourceBarrier(pTarget, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(pDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pLightGrid, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pVisualizationIntermediateTexture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			context.SetRootCBV(0, constantBuffer);
			context.SetRootCBV(1, GetViewUniforms(resources, pTarget));

			context.BindResource(2, 0, pTarget->GetSRV());
			context.BindResource(2, 1, pDepth->GetSRV());
			context.BindResource(2, 2, m_pLightGrid->GetSRV());

			context.BindResource(3, 0, m_pVisualizationIntermediateTexture->GetUAV());

			context.Dispatch(
				ComputeUtils::GetNumThreadGroups(
					pTarget->GetWidth(), 16,
					pTarget->GetHeight(), 16)
			);
			context.InsertUavBarrier();

			context.CopyTexture(m_pVisualizationIntermediateTexture, pTarget);
		});
}

void ClusteredForward::SetupPipelines()
{
	//AABB
	{
		Shader* pComputeShader = m_pDevice->GetShader("ClusterAABBGeneration.hlsl", ShaderType::Compute, "GenerateAABBs");

		m_pCreateAabbRS = new RootSignature(m_pDevice);
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

		m_pLightCullingRS = new RootSignature(m_pDevice);
		m_pLightCullingRS->FinalizeFromShader("Light Culling", pComputeShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetRootSignature(m_pLightCullingRS->GetRootSignature());
		psoDesc.SetName("Light Culling");
		m_pLightCullingPSO = m_pDevice->CreatePipeline(psoDesc);

		m_pLightCullingCommandSignature = new CommandSignature(m_pDevice);
		m_pLightCullingCommandSignature->AddDispatch();
		m_pLightCullingCommandSignature->Finalize("Light Culling Command Signature");
	}

	//Diffuse
	{
		Shader* pVertexShader = m_pDevice->GetShader("Diffuse.hlsl", ShaderType::Vertex, "VSMain", { "CLUSTERED_FORWARD" });
		Shader* pPixelShader = m_pDevice->GetShader("Diffuse.hlsl", ShaderType::Pixel, "PSMain", { "CLUSTERED_FORWARD" });

		Shader* pMeshShader = m_pDevice->GetShader("Diffuse.hlsl", ShaderType::Mesh, "MSMain", { "CLUSTERED_FORWARD" });
		Shader* pAmplificationShader = m_pDevice->GetShader("Diffuse.hlsl", ShaderType::Amplification, "ASMain", { "CLUSTERED_FORWARD" });

		m_pDiffuseRS = new RootSignature(m_pDevice);
		m_pDiffuseRS->FinalizeFromShader("Diffuse", pVertexShader);

		DXGI_FORMAT formats[] = {
			DXGI_FORMAT_R16G16B16A16_FLOAT,
			DXGI_FORMAT_R16G16B16A16_FLOAT,
		};

		{
			//Opaque
			PipelineStateInitializer psoDesc;
			psoDesc.SetRootSignature(m_pDiffuseRS->GetRootSignature());
			psoDesc.SetBlendMode(BlendMode::Replace, false);
			psoDesc.SetVertexShader(pVertexShader);
			psoDesc.SetPixelShader(pPixelShader);
			psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_EQUAL);
			psoDesc.SetDepthWrite(false);
			psoDesc.SetRenderTargetFormats(formats, ARRAYSIZE(formats), DXGI_FORMAT_D32_FLOAT, 1);
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

		if(m_pDevice->GetCapabilities().MeshShaderSupport >= D3D12_MESH_SHADER_TIER_1)
		{
			//Opaque
			PipelineStateInitializer psoDesc;
			psoDesc.SetRootSignature(m_pDiffuseRS->GetRootSignature());
			psoDesc.SetBlendMode(BlendMode::Replace, false);
			psoDesc.SetMeshShader(pMeshShader);
			psoDesc.SetAmplificationShader(pAmplificationShader);
			psoDesc.SetPixelShader(pPixelShader);
			psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_EQUAL);
			psoDesc.SetDepthWrite(false);
			psoDesc.SetRenderTargetFormats(formats, ARRAYSIZE(formats), DXGI_FORMAT_D32_FLOAT, 1);
			psoDesc.SetName("Diffuse (Opaque)");
			m_pMeshShaderDiffusePSO = m_pDevice->CreatePipeline(psoDesc);

			//Opaque Masked
			psoDesc.SetName("Diffuse Masked (Opaque)");
			psoDesc.SetCullMode(D3D12_CULL_MODE_NONE);
			m_pMeshShaderDiffuseMaskedPSO = m_pDevice->CreatePipeline(psoDesc);

			//Transparant
			psoDesc.SetName("Diffuse (Transparant)");
			psoDesc.SetBlendMode(BlendMode::Alpha, false);
			psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
			m_pMeshShaderDiffuseTransparancyPSO = m_pDevice->CreatePipeline(psoDesc);
		}
	}

	//Cluster debug rendering
	{
		Shader* pVertexShader = m_pDevice->GetShader("VisualizeLightClusters.hlsl", ShaderType::Vertex, "VSMain");
		Shader* pGeometryShader = m_pDevice->GetShader("VisualizeLightClusters.hlsl", ShaderType::Geometry, "GSMain");
		Shader* pPixelShader = m_pDevice->GetShader("VisualizeLightClusters.hlsl", ShaderType::Pixel, "PSMain");

		m_pVisualizeLightClustersRS = new RootSignature(m_pDevice);

		PipelineStateInitializer psoDesc;
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
		psoDesc.SetDepthWrite(false);
		psoDesc.SetPixelShader(pPixelShader);
		psoDesc.SetRenderTargetFormat(DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_D32_FLOAT, 1);
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

		m_pVisualizeLightsRS = new RootSignature(m_pDevice);
		m_pVisualizeLightsRS->FinalizeFromShader("Light Density Visualization", pComputeShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetRootSignature(m_pVisualizeLightsRS->GetRootSignature());
		psoDesc.SetName("Light Density Visualization");
		m_pVisualizeLightsPSO = m_pDevice->CreatePipeline(psoDesc);
	}

	{
		Shader* pComputeShader = m_pDevice->GetShader("VolumetricFog.hlsl", ShaderType::Compute, "InjectFogLightingCS", { });

		m_pVolumetricLightingRS = new RootSignature(m_pDevice);
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
