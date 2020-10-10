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

extern int g_SsrSamples;
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

				context.SetPipelineState(m_pCreateAabbPSO.get());
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
				context.SetDynamicDescriptor(1, 0, m_pAABBs->GetUAV());

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

			context.BeginRenderPass(RenderPassInfo(resources.pDepthBuffer, RenderPassAccess::Load_DontCare, true));

			context.SetPipelineState(m_pMarkUniqueClustersOpaquePSO.get());
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

			struct PerObjectParameters
			{
				Matrix World;
			} perObjectParameters{};

			perFrameParameters.LightGridParams = lightGridParams;
			perFrameParameters.ClusterDimensions = IntVector3(m_ClusterCountX, m_ClusterCountY, cClusterCountZ);
			perFrameParameters.ClusterSize = IntVector2(cClusterSize, cClusterSize);
			perFrameParameters.View = resources.pCamera->GetView();
			perFrameParameters.ViewProjection = resources.pCamera->GetViewProjection();

			context.SetDynamicConstantBufferView(1, &perFrameParameters, sizeof(PerFrameParameters));
			context.SetDynamicDescriptor(2, 0, m_pUniqueClusters->GetUAV());

			{
				GPU_PROFILE_SCOPE("Opaque", &context);
				for (const Batch& b : resources.OpaqueBatches)
				{
					perObjectParameters.World = b.WorldMatrix;
					context.SetDynamicConstantBufferView(0, &perObjectParameters, sizeof(PerObjectParameters));
					b.pMesh->Draw(&context);
				}
			}

			{
				GPU_PROFILE_SCOPE("Transparant", &context);
				context.SetPipelineState(m_pMarkUniqueClustersTransparantPSO.get());
				for (const Batch& b : resources.TransparantBatches)
				{
					perObjectParameters.World = b.WorldMatrix;
					context.SetDynamicConstantBufferView(0, &perObjectParameters, sizeof(PerObjectParameters));
					b.pMesh->Draw(&context);
				}
			}
			context.EndRenderPass();
		});

	RGPassBuilder compactClusters = graph.AddPass("Compact Clusters");
	compactClusters.Bind([=](CommandContext& context, const RGPassResources& passResources)
		{
			context.SetPipelineState(m_pCompactClustersPSO.get());
			context.SetComputeRootSignature(m_pCompactClustersRS.get());

			context.InsertResourceBarrier(m_pUniqueClusters.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pCompactedClusters.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			UnorderedAccessView* pCompactedClustersUAV = m_pCompactedClusters->GetUAV();
			context.InsertResourceBarrier(pCompactedClustersUAV->GetCounter(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			context.ClearUavUInt(m_pCompactedClusters.get(), m_pCompactedClustersRawUAV);
			context.ClearUavUInt(pCompactedClustersUAV->GetCounter(), pCompactedClustersUAV->GetCounterUAV());

			context.SetDynamicDescriptor(0, 0, m_pUniqueClusters->GetSRV());
			context.SetDynamicDescriptor(1, 0, m_pCompactedClusters->GetUAV());

			context.Dispatch(Math::RoundUp(m_ClusterCountX * m_ClusterCountY * cClusterCountZ / 64.0f));
		});

	RGPassBuilder updateArguments = graph.AddPass("Update Indirect Arguments");
	updateArguments.Bind([=](CommandContext& context, const RGPassResources& passResources)
		{
			UnorderedAccessView* pCompactedClustersUAV = m_pCompactedClusters->GetUAV();
			context.InsertResourceBarrier(pCompactedClustersUAV->GetCounter(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pIndirectArguments.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			context.SetPipelineState(m_pUpdateIndirectArgumentsPSO.get());
			context.SetComputeRootSignature(m_pUpdateIndirectArgumentsRS.get());

			context.SetDynamicDescriptor(0, 0, m_pCompactedClusters->GetUAV()->GetCounter()->GetSRV());
			context.SetDynamicDescriptor(1, 0, m_pIndirectArguments->GetUAV());

			context.Dispatch(1);
		});

	
	RGPassBuilder lightCulling = graph.AddPass("Clustered Light Culling");
	lightCulling.Bind([=](CommandContext& context, const RGPassResources& passResources)
		{
			context.SetPipelineState(m_pLightCullingPSO.get());
			context.SetComputeRootSignature(m_pLightCullingRS.get());

			context.InsertResourceBarrier(m_pIndirectArguments.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
			context.InsertResourceBarrier(m_pCompactedClusters.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pAABBs.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pLightGrid.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			context.InsertResourceBarrier(m_pLightIndexGrid.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			context.ClearUavUInt(m_pLightGrid.get(), m_pLightGridRawUAV);
			context.ClearUavUInt(m_pLightIndexCounter.get(), m_pLightIndexCounter->GetUAV());

			struct ConstantBuffer
			{
				Matrix View;
				int LightCount;
			} constantBuffer{};

			constantBuffer.View = resources.pCamera->GetView();
			constantBuffer.LightCount = (uint32)resources.pLightBuffer->GetDesc().ElementCount;

			context.SetComputeDynamicConstantBufferView(0, &constantBuffer, sizeof(ConstantBuffer));

			context.SetDynamicDescriptor(1, 0, resources.pLightBuffer->GetSRV());
			context.SetDynamicDescriptor(1, 1, m_pAABBs->GetSRV());
			context.SetDynamicDescriptor(1, 2, m_pCompactedClusters->GetSRV());

			context.SetDynamicDescriptor(2, 0, m_pLightIndexCounter->GetUAV());
			context.SetDynamicDescriptor(2, 1, m_pLightIndexGrid->GetUAV());
			context.SetDynamicDescriptor(2, 2, m_pLightGrid->GetUAV());

			context.ExecuteIndirect(m_pLightCullingCommandSignature.get(), m_pIndirectArguments.get());
		});

	RGPassBuilder basePass = graph.AddPass("Base Pass");
	basePass.Bind([=](CommandContext& context, const RGPassResources& passResources)
		{
			struct PerFrameData
			{
				Matrix View;
				Matrix Projection;
				Matrix ViewProjection;
				Vector4 ViewPosition;
				Vector2 InvScreenDimensions;
				float NearZ;
				float FarZ;
				int FrameIndex;
				int SsrSamples;
				IntVector2 padd;
				IntVector3 ClusterDimensions;
				int pad;
				IntVector2 ClusterSize;
				Vector2 LightGridParams;
			} frameData{};

			Matrix view = resources.pCamera->GetView();
			frameData.View = view;
			frameData.Projection = resources.pCamera->GetProjection();
			frameData.InvScreenDimensions = Vector2(1.0f / screenDimensions.x, 1.0f / screenDimensions.y);
			frameData.NearZ = nearZ;
			frameData.FarZ = farZ;
			frameData.ClusterDimensions = IntVector3(m_ClusterCountX, m_ClusterCountY, cClusterCountZ);
			frameData.ClusterSize = IntVector2(cClusterSize, cClusterSize);
			frameData.LightGridParams = lightGridParams;
			frameData.FrameIndex = resources.FrameIndex;
			frameData.SsrSamples = g_SsrSamples;
			frameData.ViewProjection = resources.pCamera->GetViewProjection();
			frameData.ViewPosition = Vector4(resources.pCamera->GetPosition());

			context.InsertResourceBarrier(m_pLightGrid.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pLightIndexGrid.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(resources.pRenderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
			context.InsertResourceBarrier(resources.pAO, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(resources.pPreviousColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(resources.pResolvedDepth, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

			context.BeginRenderPass(RenderPassInfo(resources.pRenderTarget, RenderPassAccess::Clear_Store, resources.pDepthBuffer, RenderPassAccess::Load_DontCare));
			context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context.SetGraphicsRootSignature(m_pDiffuseRS.get());

			context.SetDynamicConstantBufferView(1, &frameData, sizeof(PerFrameData));
			context.SetDynamicConstantBufferView(2, resources.pShadowData, sizeof(ShadowData));
			context.SetDynamicDescriptors(3, 0, resources.MaterialTextures.data(), (int)resources.MaterialTextures.size());
			context.SetDynamicDescriptor(4, 0, m_pLightGrid->GetSRV());
			context.SetDynamicDescriptor(4, 1, m_pLightIndexGrid->GetSRV());
			context.SetDynamicDescriptor(4, 2, resources.pLightBuffer->GetSRV());
			context.SetDynamicDescriptor(4, 3, resources.pAO->GetSRV());
			context.SetDynamicDescriptor(4, 4, resources.pResolvedDepth->GetSRV());
			context.SetDynamicDescriptor(4, 5, resources.pPreviousColor->GetSRV());
			int idx = 0;
			for (auto& pShadowMap : *resources.pShadowMaps)
			{
				context.SetDynamicDescriptor(5, idx++, pShadowMap->GetSRV());
			}
			context.GetCommandList()->SetGraphicsRootShaderResourceView(6, resources.pTLAS->GetGpuHandle());

			auto DrawBatches = [](CommandContext& context, const std::vector<Batch>& batches)
			{
				struct PerObjectData
				{
					Matrix World;
					MaterialData Material;
				} objectData{};

				for (const Batch& b : batches)
				{
					objectData.World = b.WorldMatrix;
					objectData.Material = b.Material;
					context.SetDynamicConstantBufferView(0, &objectData, sizeof(PerObjectData));
					b.pMesh->Draw(&context);
				}
			};

			{
				GPU_PROFILE_SCOPE("Opaque", &context);
				context.SetPipelineState(m_pDiffusePSO.get());
				DrawBatches(context, resources.OpaqueBatches);
			}

			{
				GPU_PROFILE_SCOPE("Transparant", &context);
				context.SetPipelineState(m_pDiffuseTransparancyPSO.get());
				DrawBatches(context, resources.TransparantBatches);
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

				context.BeginRenderPass(RenderPassInfo(resources.pRenderTarget, RenderPassAccess::Load_Store, resources.pDepthBuffer, RenderPassAccess::Load_DontCare));

				context.SetPipelineState(m_pDebugClustersPSO.get());
				context.SetGraphicsRootSignature(m_pDebugClustersRS.get());

				Matrix p = m_DebugClustersViewMatrix * resources.pCamera->GetViewProjection();

				context.SetDynamicConstantBufferView(0, &p, sizeof(Matrix));
				context.SetDynamicDescriptor(1, 0, m_pAABBs->GetSRV());
				context.SetDynamicDescriptor(1, 1, m_pDebugCompactedClusters->GetSRV());
				context.SetDynamicDescriptor(1, 2, m_pDebugLightGrid->GetSRV());
				context.SetDynamicDescriptor(1, 3, m_pHeatMapTexture->GetSRV());

				if (m_pDebugClustersPSO->GetType() == PipelineStateType::Mesh)
				{
					context.DispatchMesh(m_ClusterCountX* m_ClusterCountY* cClusterCountZ);
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

			context.SetPipelineState(m_pVisualizeLightsPSO.get());
			context.SetComputeRootSignature(m_pVisualizeLightsRS.get());

			context.InsertResourceBarrier(pTarget, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(pDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pLightGrid.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(m_pVisualizationIntermediateTexture.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			context.SetComputeDynamicConstantBufferView(0, &constantData, sizeof(Data));

			context.SetDynamicDescriptor(1, 0, pTarget->GetSRV());
			context.SetDynamicDescriptor(1, 1, pDepth->GetSRV());
			context.SetDynamicDescriptor(1, 2, m_pLightGrid->GetSRV());

			context.SetDynamicDescriptor(2, 0, m_pVisualizationIntermediateTexture->GetUAV());

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
		Shader computeShader = Shader("ClusterAABBGeneration.hlsl", ShaderType::Compute, "GenerateAABBs");

		m_pCreateAabbRS = std::make_unique<RootSignature>();
		m_pCreateAabbRS->FinalizeFromShader("Create AABB", computeShader, pGraphics->GetDevice());

		m_pCreateAabbPSO = std::make_unique<PipelineState>();
		m_pCreateAabbPSO->SetComputeShader(computeShader);
		m_pCreateAabbPSO->SetRootSignature(m_pCreateAabbRS->GetRootSignature());
		m_pCreateAabbPSO->Finalize("Create AABB", pGraphics->GetDevice());
	}

	//Mark Clusters
	{
		CD3DX12_INPUT_ELEMENT_DESC inputElements[] = {
			CD3DX12_INPUT_ELEMENT_DESC("POSITION", DXGI_FORMAT_R32G32B32_FLOAT),
			CD3DX12_INPUT_ELEMENT_DESC("TEXCOORD", DXGI_FORMAT_R32G32_FLOAT),
		};

		Shader vertexShader("ClusterMarking.hlsl", ShaderType::Vertex, "MarkClusters_VS");
		Shader pixelShaderOpaque("ClusterMarking.hlsl", ShaderType::Pixel, "MarkClusters_PS");

		m_pMarkUniqueClustersRS = std::make_unique<RootSignature>();
		m_pMarkUniqueClustersRS->FinalizeFromShader("Mark Unique Clusters", vertexShader, pGraphics->GetDevice());

		m_pMarkUniqueClustersOpaquePSO = std::make_unique<PipelineState>();
		m_pMarkUniqueClustersOpaquePSO->SetDepthTest(D3D12_COMPARISON_FUNC_EQUAL);
		m_pMarkUniqueClustersOpaquePSO->SetRootSignature(m_pMarkUniqueClustersRS->GetRootSignature());
		m_pMarkUniqueClustersOpaquePSO->SetVertexShader(vertexShader);
		m_pMarkUniqueClustersOpaquePSO->SetPixelShader(pixelShaderOpaque);
		m_pMarkUniqueClustersOpaquePSO->SetInputLayout(inputElements, ARRAYSIZE(inputElements));
		m_pMarkUniqueClustersOpaquePSO->SetRenderTargetFormats(nullptr, 0, Graphics::DEPTH_STENCIL_FORMAT, m_pGraphics->GetMultiSampleCount());
		m_pMarkUniqueClustersOpaquePSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
		m_pMarkUniqueClustersOpaquePSO->SetDepthWrite(false);
		m_pMarkUniqueClustersOpaquePSO->Finalize("Mark Unique Clusters", m_pGraphics->GetDevice());

		m_pMarkUniqueClustersTransparantPSO = std::make_unique<PipelineState>(*m_pMarkUniqueClustersOpaquePSO);
		m_pMarkUniqueClustersTransparantPSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
		m_pMarkUniqueClustersTransparantPSO->Finalize("Mark Unique Clusters", m_pGraphics->GetDevice());
	}

	//Compact Clusters
	{
		Shader computeShader = Shader("ClusterCompaction.hlsl", ShaderType::Compute, "CompactClusters");

		m_pCompactClustersRS = std::make_unique<RootSignature>();
		m_pCompactClustersRS->FinalizeFromShader("Compact Clusters", computeShader, pGraphics->GetDevice());

		m_pCompactClustersPSO = std::make_unique<PipelineState>();
		m_pCompactClustersPSO->SetComputeShader(computeShader);
		m_pCompactClustersPSO->SetRootSignature(m_pCompactClustersRS->GetRootSignature());
		m_pCompactClustersPSO->Finalize("Compact Clusters", pGraphics->GetDevice());
	}

	//Prepare Indirect Dispatch Buffer
	{
		Shader computeShader = Shader("ClusteredLightCullingArguments.hlsl", ShaderType::Compute, "UpdateIndirectArguments");

		m_pUpdateIndirectArgumentsRS = std::make_unique<RootSignature>();
		m_pUpdateIndirectArgumentsRS->FinalizeFromShader("Update Indirect Dispatch Buffer", computeShader, pGraphics->GetDevice());

		m_pUpdateIndirectArgumentsPSO = std::make_unique<PipelineState>();
		m_pUpdateIndirectArgumentsPSO->SetComputeShader(computeShader);
		m_pUpdateIndirectArgumentsPSO->SetRootSignature(m_pUpdateIndirectArgumentsRS->GetRootSignature());
		m_pUpdateIndirectArgumentsPSO->Finalize("Update Indirect Dispatch Buffer", pGraphics->GetDevice());
	}

	//Light Culling
	{
		Shader computeShader = Shader("ClusteredLightCulling.hlsl", ShaderType::Compute, "LightCulling");

		m_pLightCullingRS = std::make_unique<RootSignature>();
		m_pLightCullingRS->FinalizeFromShader("Light Culling", computeShader, pGraphics->GetDevice());

		m_pLightCullingPSO = std::make_unique<PipelineState>();
		m_pLightCullingPSO->SetComputeShader(computeShader);
		m_pLightCullingPSO->SetRootSignature(m_pLightCullingRS->GetRootSignature());
		m_pLightCullingPSO->Finalize("Light Culling", pGraphics->GetDevice());


		m_pLightCullingCommandSignature = std::make_unique<CommandSignature>();
		m_pLightCullingCommandSignature->AddDispatch();
		m_pLightCullingCommandSignature->Finalize("Light Culling Command Signature", pGraphics->GetDevice());
	}

	//Diffuse
	{
		CD3DX12_INPUT_ELEMENT_DESC inputElements[] = {
			CD3DX12_INPUT_ELEMENT_DESC("POSITION", DXGI_FORMAT_R32G32B32_FLOAT),
			CD3DX12_INPUT_ELEMENT_DESC("TEXCOORD", DXGI_FORMAT_R32G32_FLOAT),
			CD3DX12_INPUT_ELEMENT_DESC("NORMAL", DXGI_FORMAT_R32G32B32_FLOAT),
			CD3DX12_INPUT_ELEMENT_DESC("TANGENT", DXGI_FORMAT_R32G32B32_FLOAT),
			CD3DX12_INPUT_ELEMENT_DESC("TEXCOORD", DXGI_FORMAT_R32G32B32_FLOAT, 1),
		};

		Shader vertexShader("Diffuse.hlsl", ShaderType::Vertex, "VSMain", { "CLUSTERED_FORWARD" });
		Shader pixelShader("Diffuse.hlsl", ShaderType::Pixel, "PSMain", { "CLUSTERED_FORWARD" });

		m_pDiffuseRS = std::make_unique<RootSignature>();
		m_pDiffuseRS->FinalizeFromShader("Diffuse", vertexShader, pGraphics->GetDevice());

		//Opaque
		m_pDiffusePSO = std::make_unique<PipelineState>();
		m_pDiffusePSO->SetRootSignature(m_pDiffuseRS->GetRootSignature());
		m_pDiffusePSO->SetBlendMode(BlendMode::Replace, false);
		m_pDiffusePSO->SetVertexShader(vertexShader);
		m_pDiffusePSO->SetPixelShader(pixelShader);
		m_pDiffusePSO->SetInputLayout(inputElements, ARRAYSIZE(inputElements));
		m_pDiffusePSO->SetDepthTest(D3D12_COMPARISON_FUNC_EQUAL);
		m_pDiffusePSO->SetDepthWrite(false);
		m_pDiffusePSO->SetRenderTargetFormat(Graphics::RENDER_TARGET_FORMAT, Graphics::DEPTH_STENCIL_FORMAT, m_pGraphics->GetMultiSampleCount());
		m_pDiffusePSO->Finalize("Diffuse (Opaque)", m_pGraphics->GetDevice());

		//Transparant
		m_pDiffuseTransparancyPSO = std::make_unique<PipelineState>(*m_pDiffusePSO.get());
		m_pDiffuseTransparancyPSO->SetBlendMode(BlendMode::Alpha, false);
		m_pDiffuseTransparancyPSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
		m_pDiffuseTransparancyPSO->Finalize("Diffuse (Transparant)", m_pGraphics->GetDevice());
	}

	//Cluster debug rendering
	{
		Shader pixelShader("ClusterDebugDrawing.hlsl", ShaderType::Pixel, "PSMain");

		m_pDebugClustersRS = std::make_unique<RootSignature>();
		m_pDebugClustersPSO = std::make_unique<PipelineState>();

		m_pDebugClustersPSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
		m_pDebugClustersPSO->SetDepthWrite(false);
		m_pDebugClustersPSO->SetPixelShader(pixelShader);
		m_pDebugClustersPSO->SetRenderTargetFormat(Graphics::RENDER_TARGET_FORMAT, Graphics::DEPTH_STENCIL_FORMAT, m_pGraphics->GetMultiSampleCount());
		m_pDebugClustersPSO->SetBlendMode(BlendMode::Additive, false);

		if (m_pGraphics->SupportsMeshShaders())
		{
			Shader meshShader("ClusterDebugDrawing.hlsl", ShaderType::Mesh, "MSMain");
			m_pDebugClustersRS->FinalizeFromShader("Debug Clusters", meshShader, m_pGraphics->GetDevice());

			m_pDebugClustersPSO->SetRootSignature(m_pDebugClustersRS->GetRootSignature());
			m_pDebugClustersPSO->SetMeshShader(meshShader);
			m_pDebugClustersPSO->Finalize("Debug Clusters PSO", m_pGraphics->GetDevice());
		}
		else
		{
			Shader vertexShader("ClusterDebugDrawing.hlsl", ShaderType::Vertex, "VSMain");
			Shader geometryShader("CL_DebugDrawClusters.hlsl", ShaderType::Geometry, "GSMain");
			m_pDebugClustersRS->FinalizeFromShader("Debug Clusters", vertexShader, m_pGraphics->GetDevice());

			m_pDebugClustersPSO->SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT);
			m_pDebugClustersPSO->SetRootSignature(m_pDebugClustersRS->GetRootSignature());
			m_pDebugClustersPSO->SetVertexShader(vertexShader);
			m_pDebugClustersPSO->SetGeometryShader(geometryShader);
			m_pDebugClustersPSO->Finalize("Debug Clusters PSO", m_pGraphics->GetDevice());
		}
	}

	{
		Shader computeShader = Shader("VisualizeLightCount.hlsl", ShaderType::Compute, "DebugLightDensityCS", { "CLUSTERED_FORWARD" });

		m_pVisualizeLightsRS = std::make_unique<RootSignature>();
		m_pVisualizeLightsRS->FinalizeFromShader("Light Density Visualization", computeShader, pGraphics->GetDevice());

		m_pVisualizeLightsPSO = std::make_unique<PipelineState>();
		m_pVisualizeLightsPSO->SetComputeShader(computeShader);
		m_pVisualizeLightsPSO->SetRootSignature(m_pVisualizeLightsRS->GetRootSignature());
		m_pVisualizeLightsPSO->Finalize("Light Density Visualization", pGraphics->GetDevice());
	}
}