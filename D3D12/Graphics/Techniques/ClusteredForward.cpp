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
#include "Scene/Camera.h"

static constexpr int cClusterSize = 64;
static constexpr int cClusterCountZ = 32;

namespace Tweakables
{
	extern int g_SsrSamples;
}
bool g_VisualizeClusters = false;

ClusteredForward::ClusteredForward(Graphics* pGraphics)
	: m_pGraphics(pGraphics)
{
	SetupResources(pGraphics);
	SetupPipelines(pGraphics);
}

ClusteredForward::~ClusteredForward()
{
}


void ClusteredForward::OnSwapchainCreated(int windowWidth, int windowHeight)
{
	m_ClusterCountX = Math::RoundUp((float)windowWidth / cClusterSize);
	m_ClusterCountY = Math::RoundUp((float)windowHeight / cClusterSize);

	uint32 totalClusterCount = m_ClusterCountX * m_ClusterCountY * cClusterCountZ;
	m_pAABBs->Create(BufferDesc::CreateStructured(totalClusterCount, sizeof(Vector4) * 2));
	m_pUniqueClusters->Create(BufferDesc::CreateStructured(totalClusterCount, sizeof(uint32)));
	m_pUniqueClusters->CreateUAV(&m_pUniqueClustersRawUAV, BufferUAVDesc::CreateRaw());
	m_pDebugCompactedClusters->Create(BufferDesc::CreateStructured(totalClusterCount, sizeof(uint32)));
	m_pCompactedClusters->Create(BufferDesc::CreateStructured(totalClusterCount, sizeof(uint32)));
	m_pCompactedClusters->CreateUAV(&m_pCompactedClustersRawUAV, BufferUAVDesc::CreateRaw());
	m_pLightIndexGrid->Create(BufferDesc::CreateStructured(32 * totalClusterCount, sizeof(uint32)));
	m_pLightGrid->Create(BufferDesc::CreateStructured(totalClusterCount, 2 * sizeof(uint32)));
	m_pLightGrid->CreateUAV(&m_pLightGridRawUAV, BufferUAVDesc::CreateRaw());
	m_pDebugLightGrid->Create(BufferDesc::CreateStructured(totalClusterCount, 2 * sizeof(uint32)));

	m_ViewportDirty = true;
}

Vector2 ComputeLightGridParams(float nearZ, float farZ)
{
	Vector2 lightGridParams;
	float n = Math::Min(nearZ, farZ);
	float f = Math::Max(nearZ, farZ);
	lightGridParams.x = (float)cClusterCountZ / log(f / n);
	lightGridParams.y = ((float)cClusterCountZ * log(n)) / log(f / n);
	return lightGridParams;
}

void ClusteredForward::Execute(RGGraph& graph, const SceneData& resources)
{
	RG_GRAPH_SCOPE("Clustered Lighting", graph);

	Vector2 screenDimensions((float)resources.pRenderTarget->GetWidth(), (float)resources.pRenderTarget->GetHeight());
	float nearZ = resources.pCamera->GetNear();
	float farZ = resources.pCamera->GetFar();
	Vector2 lightGridParams = ComputeLightGridParams(nearZ, farZ);

	if (m_ViewportDirty)
	{
		RGPassBuilder calculateAabbs = graph.AddPass("Create AABBs");
		calculateAabbs.Bind([=](CommandContext& context, const RGPassResources& passResources)
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
				constantBuffer.ClusterSize = IntVector2(cClusterSize, cClusterSize);
				constantBuffer.ClusterDimensions = IntVector3(m_ClusterCountX, m_ClusterCountY, cClusterCountZ);

				context.SetComputeDynamicConstantBufferView(0, &constantBuffer, sizeof(ConstantBuffer));
				context.BindResource(1, 0, m_pAABBs->GetUAV());

				//Cluster count in z is 32 so fits nicely in a wavefront on Nvidia so make groupsize in shader 32
				context.Dispatch(m_ClusterCountX, m_ClusterCountY, Math::DivideAndRoundUp(cClusterCountZ, 32));
			});
		m_ViewportDirty = false;
	}

	RGPassBuilder markClusters = graph.AddPass("Mark Clusters");
	markClusters.Bind([=](CommandContext& context, const RGPassResources& passResources)
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
			perFrameParameters.ClusterDimensions = IntVector3(m_ClusterCountX, m_ClusterCountY, cClusterCountZ);
			perFrameParameters.ClusterSize = IntVector2(cClusterSize, cClusterSize);
			perFrameParameters.View = resources.pCamera->GetView();
			perFrameParameters.ViewProjection = resources.pCamera->GetViewProjection();

			context.SetGraphicsDynamicConstantBufferView(1, &perFrameParameters, sizeof(PerFrameParameters));
			context.BindResource(2, 0, m_pUniqueClusters->GetUAV());
			context.BindResourceTable(3, resources.GlobalSRVHeapHandle.GpuHandle, CommandListContext::Graphics);

			auto DrawBatches = [&](Batch::Blending blendMode)
			{
				struct PerObjectData
				{
					Matrix World;
					int VertexBuffer;
				} objectData;
				for (const Batch& b : resources.Batches)
				{
					if (EnumHasAnyFlags(b.BlendMode, blendMode) && resources.VisibilityMask.GetBit(b.Index))
					{
						objectData.World = b.WorldMatrix;
						objectData.VertexBuffer = b.VertexBufferDescriptor;
						context.SetGraphicsDynamicConstantBufferView(0, &objectData, sizeof(PerObjectData));
						context.SetIndexBuffer(b.pMesh->IndicesLocation);
						context.DrawIndexed(b.pMesh->IndicesLocation.Elements, 0, 0);
					}
				}
			};

			{
				GPU_PROFILE_SCOPE("Opaque", &context);
				context.SetPipelineState(m_pMarkUniqueClustersOpaquePSO);
				DrawBatches(Batch::Blending::Opaque);
			}

			{
				GPU_PROFILE_SCOPE("Transparant", &context);
				context.SetPipelineState(m_pMarkUniqueClustersTransparantPSO);
				DrawBatches(Batch::Blending::AlphaBlend | Batch::Blending::AlphaMask);
			}
			context.EndRenderPass();
		});

	RGPassBuilder compactClusters = graph.AddPass("Compact Clusters");
	compactClusters.Bind([=](CommandContext& context, const RGPassResources& passResources)
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

			context.Dispatch(Math::RoundUp(m_ClusterCountX * m_ClusterCountY * cClusterCountZ / 64.0f));
		});

	RGPassBuilder updateArguments = graph.AddPass("Update Indirect Arguments");
	updateArguments.Bind([=](CommandContext& context, const RGPassResources& passResources)
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
	lightCulling.Bind([=](CommandContext& context, const RGPassResources& passResources)
		{
			context.SetPipelineState(m_pLightCullingPSO);
			context.SetComputeRootSignature(m_pLightCullingRS.get());

			context.InsertResourceBarrier(m_pIndirectArguments.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
			context.InsertResourceBarrier(m_pCompactedClusters.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pAABBs.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pLightGrid.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			context.InsertResourceBarrier(m_pLightIndexGrid.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			context.InsertResourceBarrier(resources.pLightBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

			context.ClearUavUInt(m_pLightGrid.get(), m_pLightGridRawUAV);
			context.ClearUavUInt(m_pLightIndexCounter.get(), m_pLightIndexCounter->GetUAV());

			struct ConstantBuffer
			{
				Matrix View;
				int LightCount;
			} constantBuffer{};

			constantBuffer.View = resources.pCamera->GetView();
			constantBuffer.LightCount = resources.pLightBuffer->GetNumElements();

			context.SetComputeDynamicConstantBufferView(0, &constantBuffer, sizeof(ConstantBuffer));

			context.BindResource(1, 0, resources.pLightBuffer->GetSRV());
			context.BindResource(1, 1, m_pAABBs->GetSRV());
			context.BindResource(1, 2, m_pCompactedClusters->GetSRV());

			context.BindResource(2, 0, m_pLightIndexCounter->GetUAV());
			context.BindResource(2, 1, m_pLightIndexGrid->GetUAV());
			context.BindResource(2, 2, m_pLightGrid->GetUAV());

			context.ExecuteIndirect(m_pLightCullingCommandSignature.get(), 1, m_pIndirectArguments.get(), nullptr);
		});

	RGPassBuilder basePass = graph.AddPass("Base Pass");
	basePass.Bind([=](CommandContext& context, const RGPassResources& passResources)
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
			} frameData{};

			Matrix view = resources.pCamera->GetView();
			frameData.View = view;
			frameData.Projection = resources.pCamera->GetProjection();
			frameData.ProjectionInverse = resources.pCamera->GetProjectionInverse();
			frameData.InvScreenDimensions = Vector2(1.0f / screenDimensions.x, 1.0f / screenDimensions.y);
			frameData.NearZ = nearZ;
			frameData.FarZ = farZ;
			frameData.ClusterDimensions = IntVector3(m_ClusterCountX, m_ClusterCountY, cClusterCountZ);
			frameData.ClusterSize = IntVector2(cClusterSize, cClusterSize);
			frameData.LightGridParams = lightGridParams;
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

			context.InsertResourceBarrier(m_pLightGrid.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pLightIndexGrid.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
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

			context.SetGraphicsDynamicConstantBufferView(1, &frameData, sizeof(PerFrameData));
			context.SetGraphicsDynamicConstantBufferView(2, resources.pShadowData, sizeof(ShadowData));
			context.BindResourceTable(3, resources.GlobalSRVHeapHandle.GpuHandle, CommandListContext::Graphics);
			context.BindResource(4, 0, m_pLightGrid->GetSRV());
			context.BindResource(4, 1, m_pLightIndexGrid->GetSRV());
			context.BindResource(4, 2, resources.pLightBuffer->GetSRV());
			context.BindResource(4, 3, resources.pAO->GetSRV());
			context.BindResource(4, 4, resources.pResolvedDepth->GetSRV());
			context.BindResource(4, 5, resources.pPreviousColor->GetSRV());

			auto DrawBatches = [&](Batch::Blending blendMode)
			{
				struct PerObjectData
				{
					Matrix World;
					MaterialData Material;
					uint32 VertexBuffer;
				} objectData;
				for (const Batch& b : resources.Batches)
				{
					if (EnumHasAnyFlags(b.BlendMode, blendMode) && resources.VisibilityMask.GetBit(b.Index))
					{
						objectData.World = b.WorldMatrix;
						objectData.Material = b.Material;
						objectData.VertexBuffer = b.VertexBufferDescriptor;
						context.SetGraphicsDynamicConstantBufferView(0, &objectData, sizeof(PerObjectData));
						context.SetIndexBuffer(b.pMesh->IndicesLocation);
						context.DrawIndexed(b.pMesh->IndicesLocation.Elements, 0, 0);
					}
				}
			};

			{
				GPU_PROFILE_SCOPE("Opaque", &context);
				context.SetPipelineState(m_pDiffusePSO);
				DrawBatches(Batch::Blending::Opaque | Batch::Blending::AlphaMask);
			}

			{
				GPU_PROFILE_SCOPE("Transparant", &context);
				context.SetPipelineState(m_pDiffuseTransparancyPSO);
				DrawBatches(Batch::Blending::AlphaBlend);
			}

			context.EndRenderPass();
		});

	if (g_VisualizeClusters)
	{
		RGPassBuilder visualize = graph.AddPass("Visualize Clusters");
		visualize.Bind([=](CommandContext& context, const RGPassResources& passResources)
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

				context.SetGraphicsDynamicConstantBufferView(0, &p, sizeof(Matrix));
				context.BindResource(1, 0, m_pAABBs->GetSRV());
				context.BindResource(1, 1, m_pDebugCompactedClusters->GetSRV());
				context.BindResource(1, 2, m_pDebugLightGrid->GetSRV());
				context.BindResource(1, 3, m_pHeatMapTexture->GetSRV());

				if (m_pDebugClustersPSO->GetType() == PipelineStateType::Mesh)
				{
					context.DispatchMesh(m_ClusterCountX * m_ClusterCountY * cClusterCountZ);
				}
				else
				{
					context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
					context.Draw(0, m_ClusterCountX * m_ClusterCountY * cClusterCountZ);
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
	if (!m_pVisualizationIntermediateTexture)
	{
		m_pVisualizationIntermediateTexture = std::make_unique<Texture>(m_pGraphics, "LightDensity Debug Texture");
	}
	if (m_pVisualizationIntermediateTexture->GetDesc() != pTarget->GetDesc())
	{
		m_pVisualizationIntermediateTexture->Create(pTarget->GetDesc());
	}

	Vector2 screenDimensions((float)pTarget->GetWidth(), (float)pTarget->GetHeight());
	float nearZ = camera.GetNear();
	float farZ = camera.GetFar();
	Vector2 lightGridParams = ComputeLightGridParams(nearZ, farZ);

	RGPassBuilder basePass = graph.AddPass("Visualize Light Density");
	basePass.Bind([=](CommandContext& context, const RGPassResources& passResources)
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
			constantData.ClusterDimensions = IntVector3(m_ClusterCountX, m_ClusterCountY, cClusterCountZ);
			constantData.ClusterSize = IntVector2(cClusterSize, cClusterSize);
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

			context.SetComputeDynamicConstantBufferView(0, &constantData, sizeof(Data));

			context.BindResource(1, 0, pTarget->GetSRV());
			context.BindResource(1, 1, pDepth->GetSRV());
			context.BindResource(1, 2, m_pLightGrid->GetSRV());

			context.BindResource(2, 0, m_pVisualizationIntermediateTexture->GetUAV());

			context.Dispatch(Math::DivideAndRoundUp(pTarget->GetWidth(), 16), Math::DivideAndRoundUp(pTarget->GetHeight(), 16));
			context.InsertUavBarrier();

			context.CopyTexture(m_pVisualizationIntermediateTexture.get(), pTarget);
		});
}

void ClusteredForward::SetupResources(Graphics* pGraphics)
{
	m_pAABBs = std::make_unique<Buffer>(pGraphics, "AABBs");
	m_pUniqueClusters = std::make_unique<Buffer>(pGraphics, "Unique Clusters");
	m_pCompactedClusters = std::make_unique<Buffer>(pGraphics, "Compacted Clusters");
	m_pDebugCompactedClusters = std::make_unique<Buffer>(pGraphics, "Debug Compacted Clusters");
	m_pIndirectArguments = std::make_unique<Buffer>(pGraphics, "Light Culling Indirect Arguments");
	m_pIndirectArguments->Create(BufferDesc::CreateIndirectArguments<uint32>(3));
	m_pLightIndexCounter = std::make_unique<Buffer>(pGraphics, "Light Index Counter");
	m_pLightIndexCounter->Create(BufferDesc::CreateByteAddress(sizeof(uint32)));
	m_pLightIndexGrid = std::make_unique<Buffer>(pGraphics, "Light Index Grid");
	m_pLightGrid = std::make_unique<Buffer>(pGraphics, "Light Grid");
	m_pDebugLightGrid = std::make_unique<Buffer>(pGraphics, "Debug Light Grid");

	CommandContext* pContext = pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
	m_pHeatMapTexture = std::make_unique<Texture>(pGraphics, "Heatmap Texture");
	m_pHeatMapTexture->Create(pContext, "Resources/Textures/Heatmap.png");
	pContext->Execute(true);
}

void ClusteredForward::SetupPipelines(Graphics* pGraphics)
{
	//AABB
	{
		Shader* pComputeShader = pGraphics->GetShaderManager()->GetShader("ClusterAABBGeneration.hlsl", ShaderType::Compute, "GenerateAABBs");

		m_pCreateAabbRS = std::make_unique<RootSignature>(pGraphics);
		m_pCreateAabbRS->FinalizeFromShader("Create AABB", pComputeShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetRootSignature(m_pCreateAabbRS->GetRootSignature());
		psoDesc.SetName("Create AABB");
		m_pCreateAabbPSO = pGraphics->CreatePipeline(psoDesc);
	}

	//Mark Clusters
	{
		Shader* pVertexShader = pGraphics->GetShaderManager()->GetShader("ClusterMarking.hlsl", ShaderType::Vertex, "MarkClusters_VS");
		Shader* pPixelShaderOpaque = pGraphics->GetShaderManager()->GetShader("ClusterMarking.hlsl", ShaderType::Pixel, "MarkClusters_PS");

		m_pMarkUniqueClustersRS = std::make_unique<RootSignature>(pGraphics);
		m_pMarkUniqueClustersRS->FinalizeFromShader("Mark Unique Clusters", pVertexShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_EQUAL);
		psoDesc.SetRootSignature(m_pMarkUniqueClustersRS->GetRootSignature());
		psoDesc.SetVertexShader(pVertexShader);
		psoDesc.SetPixelShader(pPixelShaderOpaque);
		psoDesc.SetDepthOnlyTarget(Graphics::DEPTH_STENCIL_FORMAT, m_pGraphics->GetMultiSampleCount());
		psoDesc.SetDepthWrite(false);
		m_pMarkUniqueClustersOpaquePSO = pGraphics->CreatePipeline(psoDesc);

		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
		m_pMarkUniqueClustersTransparantPSO = pGraphics->CreatePipeline(psoDesc);
	}

	//Compact Clusters
	{
		Shader* pComputeShader = pGraphics->GetShaderManager()->GetShader("ClusterCompaction.hlsl", ShaderType::Compute, "CompactClusters");

		m_pCompactClustersRS = std::make_unique<RootSignature>(pGraphics);
		m_pCompactClustersRS->FinalizeFromShader("Compact Clusters", pComputeShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetRootSignature(m_pCompactClustersRS->GetRootSignature());
		psoDesc.SetName("Compact Clusters");
		m_pCompactClustersPSO = pGraphics->CreatePipeline(psoDesc);
	}

	//Prepare Indirect Dispatch Buffer
	{
		Shader* pComputeShader = pGraphics->GetShaderManager()->GetShader("ClusteredLightCullingArguments.hlsl", ShaderType::Compute, "UpdateIndirectArguments");

		m_pUpdateIndirectArgumentsRS = std::make_unique<RootSignature>(pGraphics);
		m_pUpdateIndirectArgumentsRS->FinalizeFromShader("Update Indirect Dispatch Buffer", pComputeShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetRootSignature(m_pUpdateIndirectArgumentsRS->GetRootSignature());
		psoDesc.SetName("Update Indirect Dispatch Buffer");
		m_pUpdateIndirectArgumentsPSO = pGraphics->CreatePipeline(psoDesc);
	}

	//Light Culling
	{
		Shader* pComputeShader = pGraphics->GetShaderManager()->GetShader("ClusteredLightCulling.hlsl", ShaderType::Compute, "LightCulling");

		m_pLightCullingRS = std::make_unique<RootSignature>(pGraphics);
		m_pLightCullingRS->FinalizeFromShader("Light Culling", pComputeShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetRootSignature(m_pLightCullingRS->GetRootSignature());
		psoDesc.SetName("Light Culling");
		m_pLightCullingPSO = pGraphics->CreatePipeline(psoDesc);

		m_pLightCullingCommandSignature = std::make_unique<CommandSignature>(pGraphics);
		m_pLightCullingCommandSignature->AddDispatch();
		m_pLightCullingCommandSignature->Finalize("Light Culling Command Signature");
	}

	//Diffuse
	{
		Shader* pVertexShader = pGraphics->GetShaderManager()->GetShader("Diffuse.hlsl", ShaderType::Vertex, "VSMain", { "CLUSTERED_FORWARD" });
		Shader* pPixelShader = pGraphics->GetShaderManager()->GetShader("Diffuse.hlsl", ShaderType::Pixel, "PSMain", { "CLUSTERED_FORWARD" });

		m_pDiffuseRS = std::make_unique<RootSignature>(pGraphics);
		m_pDiffuseRS->FinalizeFromShader("Diffuse", pVertexShader);

		DXGI_FORMAT formats[] = {
			Graphics::RENDER_TARGET_FORMAT,
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
		psoDesc.SetRenderTargetFormats(formats, ARRAYSIZE(formats), Graphics::DEPTH_STENCIL_FORMAT, m_pGraphics->GetMultiSampleCount());
		psoDesc.SetName("Diffuse (Opaque)");
		m_pDiffusePSO = pGraphics->CreatePipeline(psoDesc);

		//Transparant
		psoDesc.SetBlendMode(BlendMode::Alpha, false);
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
		psoDesc.SetName("Diffuse (Transparant)");
		m_pDiffuseTransparancyPSO = pGraphics->CreatePipeline(psoDesc);
	}

	//Cluster debug rendering
	{
		Shader* pPixelShader = pGraphics->GetShaderManager()->GetShader("ClusterDebugDrawing.hlsl", ShaderType::Pixel, "PSMain");

		m_pDebugClustersRS = std::make_unique<RootSignature>(pGraphics);

		PipelineStateInitializer psoDesc;
		psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
		psoDesc.SetDepthWrite(false);
		psoDesc.SetPixelShader(pPixelShader);
		psoDesc.SetRenderTargetFormat(Graphics::RENDER_TARGET_FORMAT, Graphics::DEPTH_STENCIL_FORMAT, pGraphics->GetMultiSampleCount());
		psoDesc.SetBlendMode(BlendMode::Additive, false);

		if (m_pGraphics->SupportsMeshShaders())
		{
			Shader* pMeshShader = pGraphics->GetShaderManager()->GetShader("ClusterDebugDrawing.hlsl", ShaderType::Mesh, "MSMain");
			m_pDebugClustersRS->FinalizeFromShader("Debug Clusters", pMeshShader);

			psoDesc.SetRootSignature(m_pDebugClustersRS->GetRootSignature());
			psoDesc.SetMeshShader(pMeshShader);
			psoDesc.SetName("Debug Clusters");
		}
		else
		{
			Shader* pVertexShader = pGraphics->GetShaderManager()->GetShader("ClusterDebugDrawing.hlsl", ShaderType::Vertex, "VSMain");
			Shader* pGeometryShader = pGraphics->GetShaderManager()->GetShader("ClusterDebugDrawing.hlsl", ShaderType::Geometry, "GSMain");
			m_pDebugClustersRS->FinalizeFromShader("Debug Clusters", pVertexShader);

			psoDesc.SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT);
			psoDesc.SetRootSignature(m_pDebugClustersRS->GetRootSignature());
			psoDesc.SetVertexShader(pVertexShader);
			psoDesc.SetGeometryShader(pGeometryShader);
			psoDesc.SetName("Debug Clusters");
		}
		m_pDebugClustersPSO = pGraphics->CreatePipeline(psoDesc);
	}

	{
		Shader* pComputeShader = pGraphics->GetShaderManager()->GetShader("VisualizeLightCount.hlsl", ShaderType::Compute, "DebugLightDensityCS", { "CLUSTERED_FORWARD" });

		m_pVisualizeLightsRS = std::make_unique<RootSignature>(pGraphics);
		m_pVisualizeLightsRS->FinalizeFromShader("Light Density Visualization", pComputeShader);

		PipelineStateInitializer psoDesc;
		psoDesc.SetComputeShader(pComputeShader);
		psoDesc.SetRootSignature(m_pVisualizeLightsRS->GetRootSignature());
		psoDesc.SetName("Light Density Visualization");
		m_pVisualizeLightsPSO = pGraphics->CreatePipeline(psoDesc);
	}
}
