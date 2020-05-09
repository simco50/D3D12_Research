#include "stdafx.h"
#include "ClusteredForward.h"
#include "Graphics/Core/Shader.h"
#include "Graphics/Core/PipelineState.h"
#include "Graphics/Core/RootSignature.h"
#include "Graphics/Core/GraphicsBuffer.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/Core/CommandContext.h"
#include "Graphics/Core/CommandQueue.h"
#include "Graphics/Core/Texture.h"
#include "Graphics/Core/ResourceViews.h"
#include "Graphics/Mesh.h"
#include "Graphics/Light.h"
#include "Graphics/Profiler.h"
#include "Scene/Camera.h"
#include "RenderGraph/RenderGraph.h"

static constexpr int cClusterSize = 64;
static constexpr int cClusterCountZ = 32;

bool g_VisualizeClusters = false;

ClusteredForward::ClusteredForward(Graphics* pGraphics)
	: m_pGraphics(pGraphics)
{
	SetupResources(pGraphics);
	SetupPipelines(pGraphics);
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

	CommandContext* pContext = m_pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
	{
		GPU_PROFILE_SCOPE("CreateAABBs", pContext);

		pContext->InsertResourceBarrier(m_pAABBs.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		pContext->SetPipelineState(m_pCreateAabbPSO.get());
		pContext->SetComputeRootSignature(m_pCreateAabbRS.get());

		struct ConstantBuffer
		{
			Matrix ProjectionInverse;
			Vector2 ScreenDimensions;
			Vector2 ClusterSize;
			int ClusterDimensions[3];
			float NearZ;
			float FarZ;
		} constantBuffer;

		constantBuffer.ScreenDimensions = Vector2((float)windowWidth, (float)windowHeight);
		constantBuffer.NearZ = m_pGraphics->GetCamera()->GetFar();
		constantBuffer.FarZ = m_pGraphics->GetCamera()->GetNear();
		constantBuffer.ProjectionInverse = m_pGraphics->GetCamera()->GetProjectionInverse();
		constantBuffer.ClusterSize.x = cClusterSize;
		constantBuffer.ClusterSize.y = cClusterSize;
		constantBuffer.ClusterDimensions[0] = m_ClusterCountX;
		constantBuffer.ClusterDimensions[1] = m_ClusterCountY;
		constantBuffer.ClusterDimensions[2] = cClusterCountZ;

		pContext->SetComputeDynamicConstantBufferView(0, &constantBuffer, sizeof(ConstantBuffer));
		pContext->SetDynamicDescriptor(1, 0, m_pAABBs->GetUAV());

		pContext->Dispatch(m_ClusterCountX, m_ClusterCountY, cClusterCountZ);
	}
	pContext->Execute(true);
}

void ClusteredForward::Execute(RGGraph& graph, const ClusteredForwardInputResources& resources)
{
	Vector2 screenDimensions((float)m_pGraphics->GetWindowWidth(), (float)m_pGraphics->GetWindowHeight());
	float nearZ = resources.pCamera->GetNear();
	float farZ = resources.pCamera->GetFar();

	float sliceMagicA = (float)cClusterCountZ / log(nearZ / farZ);
	float sliceMagicB = ((float)cClusterCountZ * log(farZ)) / log(nearZ / farZ);

	graph.AddPass("Mark Clusters", [&](RGPassBuilder& builder)
		{
			builder.Read(resources.DepthBuffer);
			builder.NeverCull();
			return [=](CommandContext& context, const RGPassResources& passResources)
			{
				context.InsertResourceBarrier(resources.pRenderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
				context.InsertResourceBarrier(passResources.GetTexture(resources.DepthBuffer), D3D12_RESOURCE_STATE_DEPTH_READ);
				context.InsertResourceBarrier(m_pUniqueClusters.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

				context.ClearUavUInt(m_pUniqueClusters.get(), m_pUniqueClustersRawUAV);

				context.BeginRenderPass(RenderPassInfo(passResources.GetTexture(resources.DepthBuffer), RenderPassAccess::Load_DontCare, true));

				context.SetPipelineState(m_pMarkUniqueClustersOpaquePSO.get());
				context.SetGraphicsRootSignature(m_pMarkUniqueClustersRS.get());
				context.SetViewport(FloatRect(0, 0, screenDimensions.x, screenDimensions.y));
				context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

				struct PerFrameParameters
				{
					uint32 ClusterDimensions[4];
					float ClusterSize[2];
					float SliceMagicA;
					float SliceMagicB;
				} perFrameParameters;

				struct PerObjectParameters
				{
					Matrix WorldView;
					Matrix WorldViewProjection;
				} perObjectParameters;

				perFrameParameters.SliceMagicA = sliceMagicA;
				perFrameParameters.SliceMagicB = sliceMagicB;
				perFrameParameters.ClusterDimensions[0] = m_ClusterCountX;
				perFrameParameters.ClusterDimensions[1] = m_ClusterCountY;
				perFrameParameters.ClusterDimensions[2] = cClusterCountZ;
				perFrameParameters.ClusterDimensions[3] = 0;
				perFrameParameters.ClusterSize[0] = cClusterSize;
				perFrameParameters.ClusterSize[1] = cClusterSize;

				context.SetDynamicConstantBufferView(1, &perFrameParameters, sizeof(PerFrameParameters));
				context.SetDynamicDescriptor(2, 0, m_pUniqueClusters->GetUAV());

				{
					GPU_PROFILE_SCOPE("Opaque", &context);
					for (const Batch& b : *resources.pOpaqueBatches)
					{
						perObjectParameters.WorldView = b.WorldMatrix * resources.pCamera->GetView();
						perObjectParameters.WorldViewProjection = b.WorldMatrix * resources.pCamera->GetViewProjection();

						context.SetDynamicConstantBufferView(0, &perObjectParameters, sizeof(PerObjectParameters));
						b.pMesh->Draw(&context);
					}
				}

				{
					GPU_PROFILE_SCOPE("Transparant", &context);
					context.SetPipelineState(m_pMarkUniqueClustersTransparantPSO.get());
					for (const Batch& b : *resources.pTransparantBatches)
					{
						perObjectParameters.WorldView = b.WorldMatrix * resources.pCamera->GetView();
						perObjectParameters.WorldViewProjection = b.WorldMatrix * resources.pCamera->GetViewProjection();
						
						context.SetDynamicConstantBufferView(0, &perObjectParameters, sizeof(PerObjectParameters));

						context.SetDynamicDescriptor(3, 0, b.pMaterial->pDiffuseTexture->GetSRV());
						b.pMesh->Draw(&context);
					}
				}
				context.EndRenderPass();
			};
		});

	graph.AddPass("Compact Clusters", [&](RGPassBuilder& builder)
		{
			builder.NeverCull();
			return [=](CommandContext& context, const RGPassResources& resources)
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

				context.Dispatch(Math::RoundUp(m_ClusterCountX * m_ClusterCountY * cClusterCountZ / 64.0f), 1, 1);
			};
		});

	graph.AddPass("Update Indirect Arguments", [&](RGPassBuilder& builder)
		{
			builder.NeverCull();
			return [=](CommandContext& context, const RGPassResources& resources)
			{
				UnorderedAccessView* pCompactedClustersUAV = m_pCompactedClusters->GetUAV();
				context.InsertResourceBarrier(pCompactedClustersUAV->GetCounter(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				context.InsertResourceBarrier(m_pIndirectArguments.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

				context.SetPipelineState(m_pUpdateIndirectArgumentsPSO.get());
				context.SetComputeRootSignature(m_pUpdateIndirectArgumentsRS.get());

				context.SetDynamicDescriptor(0, 0, m_pCompactedClusters->GetUAV()->GetCounter()->GetSRV());
				context.SetDynamicDescriptor(1, 0, m_pIndirectArguments->GetUAV());

				context.Dispatch(1, 1, 1);
			};
		});

	
	graph.AddPass("Clustered Light Culling", [&](RGPassBuilder& builder)
		{
			builder.NeverCull();
			return [=](CommandContext& context, const RGPassResources& passResources)
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
				} constantBuffer;

				constantBuffer.View = resources.pCamera->GetView();
				constantBuffer.LightCount = (uint32)resources.pLightBuffer->GetDesc().ElementCount;

				context.SetComputeDynamicConstantBufferView(0, &constantBuffer, sizeof(ConstantBuffer));

				context.SetDynamicDescriptor(1, 0, resources.pLightBuffer->GetSRV());
				context.SetDynamicDescriptor(1, 1, m_pAABBs->GetSRV());
				context.SetDynamicDescriptor(1, 2, m_pCompactedClusters->GetSRV());

				context.SetDynamicDescriptor(2, 0, m_pLightIndexCounter->GetUAV());
				context.SetDynamicDescriptor(2, 1, m_pLightIndexGrid->GetUAV());
				context.SetDynamicDescriptor(2, 2, m_pLightGrid->GetUAV());

				context.ExecuteIndirect(m_pLightCullingCommandSignature.Get(), m_pIndirectArguments.get());
			};
		});

	graph.AddPass("Base Pass", [&](RGPassBuilder& builder)
		{
			builder.Read(resources.DepthBuffer);
			builder.NeverCull();
			return [=](CommandContext& context, const RGPassResources& passResources)
			{
				struct PerObjectData
				{
					Matrix World;
					Matrix WorldViewProjection;
				} objectData;

				struct PerFrameData
				{
					Matrix View;
					Matrix Projection;
					Matrix ViewInverse;
					uint32 ClusterDimensions[4];
					Vector2 ScreenDimensions;
					float NearZ;
					float FarZ;
					float ClusterSize[2];
					float SliceMagicA;
					float SliceMagicB;
				} frameData;

				Matrix view = resources.pCamera->GetView();
				frameData.View = view;
				frameData.Projection = resources.pCamera->GetProjection();
				frameData.ViewInverse = resources.pCamera->GetViewInverse();
				frameData.ScreenDimensions = screenDimensions;
				frameData.NearZ = farZ;
				frameData.FarZ = nearZ;
				frameData.ClusterDimensions[0] = m_ClusterCountX;
				frameData.ClusterDimensions[1] = m_ClusterCountY;
				frameData.ClusterDimensions[2] = cClusterCountZ;
				frameData.ClusterDimensions[3] = 0;
				frameData.ClusterSize[0] = cClusterSize;
				frameData.ClusterSize[1] = cClusterSize;
				frameData.SliceMagicA = sliceMagicA;
				frameData.SliceMagicB = sliceMagicB;

				context.InsertResourceBarrier(m_pLightGrid.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
				context.InsertResourceBarrier(m_pLightIndexGrid.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
				context.InsertResourceBarrier(resources.pRenderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
				context.InsertResourceBarrier(resources.pAO, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

				context.BeginRenderPass(RenderPassInfo(resources.pRenderTarget, RenderPassAccess::Clear_Store, passResources.GetTexture(resources.DepthBuffer), RenderPassAccess::Load_DontCare));
				context.SetViewport(FloatRect(0, 0, (float)screenDimensions.x, (float)screenDimensions.y));
				context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				context.SetGraphicsRootSignature(m_pDiffuseRS.get());
				{
					GPU_PROFILE_SCOPE("Opaque", &context);
					context.SetPipelineState(m_pDiffusePSO.get());

					context.SetDynamicConstantBufferView(1, &frameData, sizeof(PerFrameData));
					context.SetDynamicConstantBufferView(2, resources.pShadowData, sizeof(ShadowData));
					context.SetDynamicDescriptor(4, 0, resources.pShadowMap->GetSRV());
					context.SetDynamicDescriptor(4, 1, m_pLightGrid->GetSRV());
					context.SetDynamicDescriptor(4, 2, m_pLightIndexGrid->GetSRV());
					context.SetDynamicDescriptor(4, 3, resources.pLightBuffer->GetSRV());
					context.SetDynamicDescriptor(4, 4, resources.pAO->GetSRV());


					for (const Batch& b : *resources.pOpaqueBatches)
					{
						objectData.World = b.WorldMatrix;
						objectData.WorldViewProjection = objectData.World * resources.pCamera->GetViewProjection();

						context.SetDynamicConstantBufferView(0, &objectData, sizeof(PerObjectData));
						context.SetDynamicDescriptor(3, 0, b.pMaterial->pDiffuseTexture->GetSRV());
						context.SetDynamicDescriptor(3, 1, b.pMaterial->pNormalTexture->GetSRV());
						context.SetDynamicDescriptor(3, 2, b.pMaterial->pSpecularTexture->GetSRV());
						b.pMesh->Draw(&context);
					}
				}

				{
					GPU_PROFILE_SCOPE("Transparant", &context);
					context.SetPipelineState(m_pDiffuseTransparancyPSO.get());

					for (const Batch& b : *resources.pTransparantBatches)
					{
						objectData.World = b.WorldMatrix;
						objectData.WorldViewProjection = objectData.World * resources.pCamera->GetViewProjection();

						context.SetDynamicConstantBufferView(0, &objectData, sizeof(PerObjectData));
						context.SetDynamicDescriptor(3, 0, b.pMaterial->pDiffuseTexture->GetSRV());
						context.SetDynamicDescriptor(3, 1, b.pMaterial->pNormalTexture->GetSRV());
						context.SetDynamicDescriptor(3, 2, b.pMaterial->pSpecularTexture->GetSRV());
						b.pMesh->Draw(&context);
					}
				}

				context.EndRenderPass();
			};
		});

	if (g_VisualizeClusters)
	{
		graph.AddPass("Visualize Clusters", [&](RGPassBuilder& builder)
			{
				builder.Read(resources.DepthBuffer);
				builder.NeverCull();
				return [=](CommandContext& context, const RGPassResources& passResources)
				{
					if (m_DidCopyDebugClusterData == false)
					{
						context.CopyResource(m_pCompactedClusters.get(), m_pDebugCompactedClusters.get());
						context.CopyResource(m_pLightGrid.get(), m_pDebugLightGrid.get());
						m_DebugClustersViewMatrix = resources.pCamera->GetView();
						m_DebugClustersViewMatrix.Invert(m_DebugClustersViewMatrix);
						m_DidCopyDebugClusterData = true;
					}

					context.BeginRenderPass(RenderPassInfo(resources.pRenderTarget, RenderPassAccess::Load_Store, passResources.GetTexture(resources.DepthBuffer), RenderPassAccess::Load_DontCare));

					context.SetPipelineState(m_pDebugClustersPSO.get());
					context.SetGraphicsRootSignature(m_pDebugClustersRS.get());

					context.SetViewport(FloatRect(0, 0, (float)screenDimensions.x, (float)screenDimensions.y));
					context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);

					Matrix p = m_DebugClustersViewMatrix * resources.pCamera->GetViewProjection();

					context.SetDynamicConstantBufferView(0, &p, sizeof(Matrix));
					context.SetDynamicDescriptor(1, 0, m_pAABBs->GetSRV());
					context.SetDynamicDescriptor(1, 1, m_pDebugCompactedClusters->GetSRV());
					context.SetDynamicDescriptor(1, 2, m_pDebugLightGrid->GetSRV());
					context.SetDynamicDescriptor(1, 3, m_pHeatMapTexture->GetSRV());
					context.Draw(0, m_ClusterCountX * m_ClusterCountY * cClusterCountZ);

					context.EndRenderPass();
				};
			});
	}
	else
	{
		m_DidCopyDebugClusterData = false;
	}
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

	CommandContext* pContext = pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_COPY);
	m_pHeatMapTexture = std::make_unique<Texture>(pGraphics, "Heatmap Texture");
	m_pHeatMapTexture->Create(pContext, "Resources/Textures/Heatmap.png");
	pContext->Execute(true);
}

void ClusteredForward::SetupPipelines(Graphics* pGraphics)
{
	//AABB
	{
		Shader computeShader = Shader("Resources/Shaders/CL_GenerateAABBs.hlsl", Shader::Type::Compute, "GenerateAABBs");

		m_pCreateAabbRS = std::make_unique<RootSignature>();
		m_pCreateAabbRS->FinalizeFromShader("Create AABB", computeShader, pGraphics->GetDevice());

		m_pCreateAabbPSO = std::make_unique<PipelineState>();
		m_pCreateAabbPSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pCreateAabbPSO->SetRootSignature(m_pCreateAabbRS->GetRootSignature());
		m_pCreateAabbPSO->Finalize("Create AABB", pGraphics->GetDevice());
	}

	//Mark Clusters
	{
		D3D12_INPUT_ELEMENT_DESC inputElements[] = {
			D3D12_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};

		Shader vertexShader("Resources/Shaders/CL_MarkUniqueClusters.hlsl", Shader::Type::Vertex, "MarkClusters_VS");
		Shader pixelShaderOpaque("Resources/Shaders/CL_MarkUniqueClusters.hlsl", Shader::Type::Pixel, "MarkClusters_PS");

		m_pMarkUniqueClustersRS = std::make_unique<RootSignature>();
		m_pMarkUniqueClustersRS->FinalizeFromShader("Mark Unique Clusters", vertexShader, pGraphics->GetDevice());

		m_pMarkUniqueClustersOpaquePSO = std::make_unique<PipelineState>();
		m_pMarkUniqueClustersOpaquePSO->SetDepthTest(D3D12_COMPARISON_FUNC_EQUAL);
		m_pMarkUniqueClustersOpaquePSO->SetRootSignature(m_pMarkUniqueClustersRS->GetRootSignature());
		m_pMarkUniqueClustersOpaquePSO->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
		m_pMarkUniqueClustersOpaquePSO->SetPixelShader(pixelShaderOpaque.GetByteCode(), pixelShaderOpaque.GetByteCodeSize());
		m_pMarkUniqueClustersOpaquePSO->SetInputLayout(inputElements, ARRAYSIZE(inputElements));
		m_pMarkUniqueClustersOpaquePSO->SetRenderTargetFormats(nullptr, 0, Graphics::DEPTH_STENCIL_FORMAT, m_pGraphics->GetMultiSampleCount(), m_pGraphics->GetMultiSampleQualityLevel(m_pGraphics->GetMultiSampleCount()));
		m_pMarkUniqueClustersOpaquePSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
		m_pMarkUniqueClustersOpaquePSO->SetDepthWrite(false);
		m_pMarkUniqueClustersOpaquePSO->Finalize("Mark Unique Clusters", m_pGraphics->GetDevice());

		m_pMarkUniqueClustersTransparantPSO = std::make_unique<PipelineState>(*m_pMarkUniqueClustersOpaquePSO);
		m_pMarkUniqueClustersOpaquePSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
		m_pMarkUniqueClustersTransparantPSO->Finalize("Mark Unique Clusters", m_pGraphics->GetDevice());
	}

	//Compact Clusters
	{
		Shader computeShader = Shader("Resources/Shaders/CL_CompactClusters.hlsl", Shader::Type::Compute, "CompactClusters");

		m_pCompactClustersRS = std::make_unique<RootSignature>();
		m_pCompactClustersRS->FinalizeFromShader("Compact Clusters", computeShader, pGraphics->GetDevice());

		m_pCompactClustersPSO = std::make_unique<PipelineState>();
		m_pCompactClustersPSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pCompactClustersPSO->SetRootSignature(m_pCompactClustersRS->GetRootSignature());
		m_pCompactClustersPSO->Finalize("Compact Clusters", pGraphics->GetDevice());
	}

	//Prepare Indirect Dispatch Buffer
	{
		Shader computeShader = Shader("Resources/Shaders/CL_UpdateIndirectArguments.hlsl", Shader::Type::Compute, "UpdateIndirectArguments");

		m_pUpdateIndirectArgumentsRS = std::make_unique<RootSignature>();
		m_pUpdateIndirectArgumentsRS->FinalizeFromShader("Update Indirect Dispatch Buffer", computeShader, pGraphics->GetDevice());

		m_pUpdateIndirectArgumentsPSO = std::make_unique<PipelineState>();
		m_pUpdateIndirectArgumentsPSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pUpdateIndirectArgumentsPSO->SetRootSignature(m_pUpdateIndirectArgumentsRS->GetRootSignature());
		m_pUpdateIndirectArgumentsPSO->Finalize("Update Indirect Dispatch Buffer", pGraphics->GetDevice());
	}

	//Light Culling
	{
		Shader computeShader = Shader("Resources/Shaders/CL_LightCulling.hlsl", Shader::Type::Compute, "LightCulling");

		m_pLightCullingRS = std::make_unique<RootSignature>();
		m_pLightCullingRS->FinalizeFromShader("Light Culling", computeShader, pGraphics->GetDevice());

		m_pLightCullingPSO = std::make_unique<PipelineState>();
		m_pLightCullingPSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pLightCullingPSO->SetRootSignature(m_pLightCullingRS->GetRootSignature());
		m_pLightCullingPSO->Finalize("Light Culling", pGraphics->GetDevice());

		D3D12_INDIRECT_ARGUMENT_DESC desc;
		desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
		D3D12_COMMAND_SIGNATURE_DESC sigDesc = {};
		sigDesc.ByteStride = 3 * sizeof(uint32);
		sigDesc.NodeMask = 0;
		sigDesc.pArgumentDescs = &desc;
		sigDesc.NumArgumentDescs = 1;
		HR(m_pGraphics->GetDevice()->CreateCommandSignature(&sigDesc, nullptr, IID_PPV_ARGS(m_pLightCullingCommandSignature.GetAddressOf())));
	}

	//Diffuse
	{
		D3D12_INPUT_ELEMENT_DESC inputElements[] = {
			D3D12_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			D3D12_INPUT_ELEMENT_DESC{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			D3D12_INPUT_ELEMENT_DESC{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 1, DXGI_FORMAT_R32G32B32_FLOAT, 0, 44, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};

		Shader vertexShader("Resources/Shaders/CL_Diffuse.hlsl", Shader::Type::Vertex, "VSMain", { });
		Shader pixelShader("Resources/Shaders/CL_Diffuse.hlsl", Shader::Type::Pixel, "PSMain", { });

		m_pDiffuseRS = std::make_unique<RootSignature>();
		m_pDiffuseRS->FinalizeFromShader("Diffuse", vertexShader, pGraphics->GetDevice());

		//Opaque
		m_pDiffusePSO = std::make_unique<PipelineState>();
		m_pDiffusePSO->SetRootSignature(m_pDiffuseRS->GetRootSignature());
		m_pDiffusePSO->SetBlendMode(BlendMode::Replace, false);
		m_pDiffusePSO->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
		m_pDiffusePSO->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
		m_pDiffusePSO->SetInputLayout(inputElements, ARRAYSIZE(inputElements));
		m_pDiffusePSO->SetDepthTest(D3D12_COMPARISON_FUNC_EQUAL);
		m_pDiffusePSO->SetDepthWrite(false);
		m_pDiffusePSO->SetRenderTargetFormat(Graphics::RENDER_TARGET_FORMAT, Graphics::DEPTH_STENCIL_FORMAT, m_pGraphics->GetMultiSampleCount(), m_pGraphics->GetMultiSampleQualityLevel(m_pGraphics->GetMultiSampleCount()));
		m_pDiffusePSO->Finalize("Diffuse (Opaque)", m_pGraphics->GetDevice());

		//Transparant
		m_pDiffuseTransparancyPSO = std::make_unique<PipelineState>(*m_pDiffusePSO.get());
		m_pDiffuseTransparancyPSO->SetBlendMode(BlendMode::Alpha, false);
		m_pDiffuseTransparancyPSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
		m_pDiffuseTransparancyPSO->Finalize("Diffuse (Transparant)", m_pGraphics->GetDevice());
	}

	//Cluster debug rendering
	{
		Shader vertexShader("Resources/Shaders/CL_DebugDrawClusters.hlsl", Shader::Type::Vertex, "VSMain");
		Shader geometryShader("Resources/Shaders/CL_DebugDrawClusters.hlsl", Shader::Type::Geometry, "GSMain");
		Shader pixelShader("Resources/Shaders/CL_DebugDrawClusters.hlsl", Shader::Type::Pixel, "PSMain");

		m_pDebugClustersRS = std::make_unique<RootSignature>();
		m_pDebugClustersRS->FinalizeFromShader("Debug Clusters", vertexShader, m_pGraphics->GetDevice());

		m_pDebugClustersPSO = std::make_unique<PipelineState>();
		m_pDebugClustersPSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
		m_pDebugClustersPSO->SetDepthWrite(false);
		m_pDebugClustersPSO->SetInputLayout(nullptr, 0);
		m_pDebugClustersPSO->SetRootSignature(m_pDebugClustersRS->GetRootSignature());
		m_pDebugClustersPSO->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
		m_pDebugClustersPSO->SetGeometryShader(geometryShader.GetByteCode(), geometryShader.GetByteCodeSize());
		m_pDebugClustersPSO->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
		m_pDebugClustersPSO->SetRenderTargetFormat(Graphics::RENDER_TARGET_FORMAT, Graphics::DEPTH_STENCIL_FORMAT, m_pGraphics->GetMultiSampleCount(), m_pGraphics->GetMultiSampleQualityLevel(m_pGraphics->GetMultiSampleCount()));
		m_pDebugClustersPSO->SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT);
		m_pDebugClustersPSO->SetBlendMode(BlendMode::And, false);
		m_pDebugClustersPSO->Finalize("Debug Clusters PSO", m_pGraphics->GetDevice());
	}
}