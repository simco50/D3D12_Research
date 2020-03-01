#include "stdafx.h"
#include "ClusteredForward.h"
#include "Graphics/Shader.h"
#include "Graphics/PipelineState.h"
#include "Graphics/RootSignature.h"
#include "Graphics/GraphicsBuffer.h"
#include "Graphics/Graphics.h"
#include "Graphics/CommandContext.h"
#include "Graphics/CommandQueue.h"
#include "Graphics/Texture.h"
#include "Graphics/Mesh.h"
#include "Graphics/Light.h"
#include "Graphics/Profiler.h"
#include "Scene/Camera.h"
#include "ResourceViews.h"
#include "RenderGraph/RenderGraph.h"

static constexpr int cClusterSize = 64;
static constexpr int cClusterCountZ = 32;

bool gVisualizeClusters = false;

ClusteredForward::ClusteredForward(Graphics* pGraphics)
	: m_pGraphics(pGraphics)
{
	SetupResources(pGraphics);
	SetupPipelines(pGraphics);
}

void ClusteredForward::OnSwapchainCreated(int windowWidth, int windowHeight)
{
	m_pDepthTexture->Create(TextureDesc::CreateDepth(windowWidth, windowHeight, Graphics::DEPTH_STENCIL_FORMAT, TextureFlag::DepthStencil, m_pGraphics->GetMultiSampleCount(), ClearBinding(0.0f, 0)));

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
	// Create AABBs
	{
		GPU_PROFILE_SCOPE("CreateAABBs", pContext);

		pContext->SetComputePipelineState(m_pCreateAabbPSO.get());
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

		constantBuffer.ScreenDimensions = Vector2((float)m_pGraphics->GetWindowWidth(), (float)m_pGraphics->GetWindowHeight());
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
			builder.NeverCull();
			return [=](CommandContext& context, const RGPassResources& passResources)
			{
				context.InsertResourceBarrier(m_pDepthTexture.get(), D3D12_RESOURCE_STATE_DEPTH_WRITE);
				context.InsertResourceBarrier(resources.pRenderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
				context.InsertResourceBarrier(m_pUniqueClusters.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

				context.ClearUavUInt(m_pUniqueClusters.get(), m_pUniqueClustersRawUAV);

				context.BeginRenderPass(RenderPassInfo(m_pDepthTexture.get(), RenderPassAccess::Clear_Store, true));

				context.SetGraphicsPipelineState(m_pMarkUniqueClustersOpaquePSO.get());
				context.SetGraphicsRootSignature(m_pMarkUniqueClustersRS.get());
				context.SetViewport(FloatRect(0, 0, screenDimensions.x, screenDimensions.y));
				context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

				struct ConstantBuffer
				{
					Matrix WorldView;
					Matrix Projection;
					uint32 ClusterDimensions[4];
					float ClusterSize[2];
					float SliceMagicA;
					float SliceMagicB;
				} constantBuffer;

				constantBuffer.WorldView = resources.pCamera->GetView();
				constantBuffer.Projection = resources.pCamera->GetProjection();
				constantBuffer.SliceMagicA = sliceMagicA;
				constantBuffer.SliceMagicB = sliceMagicB;
				constantBuffer.ClusterDimensions[0] = m_ClusterCountX;
				constantBuffer.ClusterDimensions[1] = m_ClusterCountY;
				constantBuffer.ClusterDimensions[2] = cClusterCountZ;
				constantBuffer.ClusterDimensions[3] = 0;
				constantBuffer.ClusterSize[0] = cClusterSize;
				constantBuffer.ClusterSize[1] = cClusterSize;

				{
					GPU_PROFILE_SCOPE("Opaque", &context);
					context.SetDynamicConstantBufferView(0, &constantBuffer, sizeof(ConstantBuffer));
					context.SetDynamicDescriptor(1, 0, m_pUniqueClusters->GetUAV());
					for (const Batch& b : *resources.pOpaqueBatches)
					{
						b.pMesh->Draw(&context);
					}
				}

				{
					GPU_PROFILE_SCOPE("Transparant", &context);
					context.SetGraphicsPipelineState(m_pMarkUniqueClustersTransparantPSO.get());
					for (const Batch& b : *resources.pTransparantBatches)
					{
						context.SetDynamicDescriptor(2, 0, b.pMaterial->pDiffuseTexture->GetSRV());
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
				context.SetComputePipelineState(m_pCompactClustersPSO.get());
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

				context.SetComputePipelineState(m_pUpdateIndirectArgumentsPSO.get());
				context.SetComputeRootSignature(m_pUpdateIndirectArgumentsRS.get());

				context.SetDynamicDescriptor(0, 0, m_pCompactedClusters->GetUAV()->GetCounter()->GetSRV());
				context.SetDynamicDescriptor(1, 0, m_pIndirectArguments->GetUAV());

				context.Dispatch(1, 1, 1);
			};
		});

	
	graph.AddPass("Light Culling", [&](RGPassBuilder& builder)
		{
			builder.NeverCull();
			return [=](CommandContext& context, const RGPassResources& passResources)
			{
				context.SetComputePipelineState(m_pLightCullingPSO.get());
				context.SetComputeRootSignature(m_pLightCullingRS.get());

				context.ClearUavUInt(m_pLightIndexCounter.get(), m_pLightIndexCounter->GetUAV());

				context.InsertResourceBarrier(m_pIndirectArguments.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
				context.InsertResourceBarrier(m_pCompactedClusters.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				context.InsertResourceBarrier(m_pAABBs.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				context.InsertResourceBarrier(m_pLightGrid.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				context.InsertResourceBarrier(m_pLightIndexGrid.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

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
			builder.NeverCull();
			return [=](CommandContext& context, const RGPassResources& passResources)
			{
				struct PerObjectData
				{
					Matrix World;
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
				context.InsertResourceBarrier(m_pDepthTexture.get(), D3D12_RESOURCE_STATE_DEPTH_READ);

				context.BeginRenderPass(RenderPassInfo(resources.pRenderTarget, RenderPassAccess::Clear_Store, m_pDepthTexture.get(), RenderPassAccess::Load_DontCare));
				context.SetViewport(FloatRect(0, 0, (float)screenDimensions.x, (float)screenDimensions.y));
				context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				context.SetGraphicsRootSignature(m_pDiffuseRS.get());
				{
					GPU_PROFILE_SCOPE("Opaque", &context);
					context.SetGraphicsPipelineState(m_pDiffusePSO.get());

					context.SetDynamicConstantBufferView(1, &frameData, sizeof(PerFrameData));
					context.SetDynamicDescriptor(3, 0, m_pLightGrid->GetSRV());
					context.SetDynamicDescriptor(3, 1, m_pLightIndexGrid->GetSRV());
					context.SetDynamicDescriptor(3, 2, resources.pLightBuffer->GetSRV());

					for (const Batch& b : *resources.pOpaqueBatches)
					{
						objectData.World = Matrix::Identity;
						context.SetDynamicConstantBufferView(0, &objectData, sizeof(PerObjectData));
						context.SetDynamicDescriptor(2, 0, b.pMaterial->pDiffuseTexture->GetSRV());
						context.SetDynamicDescriptor(2, 1, b.pMaterial->pNormalTexture->GetSRV());
						context.SetDynamicDescriptor(2, 2, b.pMaterial->pSpecularTexture->GetSRV());
						b.pMesh->Draw(&context);
					}
				}

				{
					GPU_PROFILE_SCOPE("Transparant", &context);
					context.SetGraphicsPipelineState(m_pDiffuseTransparancyPSO.get());

					for (const Batch& b : *resources.pTransparantBatches)
					{
						objectData.World = Matrix::Identity;
						context.SetDynamicConstantBufferView(0, &objectData, sizeof(PerObjectData));
						context.SetDynamicDescriptor(2, 0, b.pMaterial->pDiffuseTexture->GetSRV());
						context.SetDynamicDescriptor(2, 1, b.pMaterial->pNormalTexture->GetSRV());
						context.SetDynamicDescriptor(2, 2, b.pMaterial->pSpecularTexture->GetSRV());
						b.pMesh->Draw(&context);
					}
				}

				context.EndRenderPass();
			};
		});

	if (gVisualizeClusters)
	{
		graph.AddPass("Base Pass", [&](RGPassBuilder& builder)
			{
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

					context.BeginRenderPass(RenderPassInfo(resources.pRenderTarget, RenderPassAccess::Load_Store, m_pDepthTexture.get(), RenderPassAccess::Load_DontCare));

					context.SetGraphicsPipelineState(m_pDebugClustersPSO.get());
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
	m_pDepthTexture = std::make_unique<Texture>(pGraphics, "Depth Texture");

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
		Shader computeShader = Shader("Resources/Shaders/CL_GenerateAABBs.hlsl", Shader::Type::ComputeShader, "GenerateAABBs");

		m_pCreateAabbRS = std::make_unique<RootSignature>();
		m_pCreateAabbRS->FinalizeFromShader("Create AABB", computeShader, pGraphics->GetDevice());

		m_pCreateAabbPSO = std::make_unique<ComputePipelineState>();
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

		Shader vertexShader("Resources/Shaders/CL_MarkUniqueClusters.hlsl", Shader::Type::VertexShader, "MarkClusters_VS");
		Shader pixelShaderOpaque("Resources/Shaders/CL_MarkUniqueClusters.hlsl", Shader::Type::PixelShader, "MarkClusters_PS");

		m_pMarkUniqueClustersRS = std::make_unique<RootSignature>();
		m_pMarkUniqueClustersRS->FinalizeFromShader("Mark Unique Clusters", vertexShader, pGraphics->GetDevice());

		m_pMarkUniqueClustersOpaquePSO = std::make_unique<GraphicsPipelineState>();
		m_pMarkUniqueClustersOpaquePSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
		m_pMarkUniqueClustersOpaquePSO->SetRootSignature(m_pMarkUniqueClustersRS->GetRootSignature());
		m_pMarkUniqueClustersOpaquePSO->SetBlendMode(BlendMode::Replace, false);
		m_pMarkUniqueClustersOpaquePSO->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
		m_pMarkUniqueClustersOpaquePSO->SetPixelShader(pixelShaderOpaque.GetByteCode(), pixelShaderOpaque.GetByteCodeSize());
		m_pMarkUniqueClustersOpaquePSO->SetInputLayout(inputElements, ARRAYSIZE(inputElements));
		m_pMarkUniqueClustersOpaquePSO->SetRenderTargetFormats(nullptr, 0, Graphics::DEPTH_STENCIL_FORMAT, m_pGraphics->GetMultiSampleCount(), m_pGraphics->GetMultiSampleQualityLevel(m_pGraphics->GetMultiSampleCount()));
		m_pMarkUniqueClustersOpaquePSO->Finalize("Mark Unique Clusters", m_pGraphics->GetDevice());

		m_pMarkUniqueClustersTransparantPSO = std::make_unique<GraphicsPipelineState>(*m_pMarkUniqueClustersOpaquePSO);
		m_pMarkUniqueClustersTransparantPSO->SetDepthWrite(false);
		m_pMarkUniqueClustersTransparantPSO->Finalize("Mark Unique Clusters", m_pGraphics->GetDevice());
	}

	//Compact Clusters
	{
		Shader computeShader = Shader("Resources/Shaders/CL_CompactClusters.hlsl", Shader::Type::ComputeShader, "CompactClusters");

		m_pCompactClustersRS = std::make_unique<RootSignature>();
		m_pCompactClustersRS->FinalizeFromShader("Compact Clusters", computeShader, pGraphics->GetDevice());

		m_pCompactClustersPSO = std::make_unique<ComputePipelineState>();
		m_pCompactClustersPSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pCompactClustersPSO->SetRootSignature(m_pCompactClustersRS->GetRootSignature());
		m_pCompactClustersPSO->Finalize("Compact Clusters", pGraphics->GetDevice());
	}

	//Prepare Indirect Dispatch Buffer
	{
		Shader computeShader = Shader("Resources/Shaders/CL_UpdateIndirectArguments.hlsl", Shader::Type::ComputeShader, "UpdateIndirectArguments");

		m_pUpdateIndirectArgumentsRS = std::make_unique<RootSignature>();
		m_pUpdateIndirectArgumentsRS->FinalizeFromShader("Update Indirect Dispatch Buffer", computeShader, pGraphics->GetDevice());

		m_pUpdateIndirectArgumentsPSO = std::make_unique<ComputePipelineState>();
		m_pUpdateIndirectArgumentsPSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pUpdateIndirectArgumentsPSO->SetRootSignature(m_pUpdateIndirectArgumentsRS->GetRootSignature());
		m_pUpdateIndirectArgumentsPSO->Finalize("Update Indirect Dispatch Buffer", pGraphics->GetDevice());
	}

	//Light Culling
	{
		Shader computeShader = Shader("Resources/Shaders/CL_LightCulling.hlsl", Shader::Type::ComputeShader, "LightCulling");

		m_pLightCullingRS = std::make_unique<RootSignature>();
		m_pLightCullingRS->FinalizeFromShader("Light Culling", computeShader, pGraphics->GetDevice());

		m_pLightCullingPSO = std::make_unique<ComputePipelineState>();
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

		Shader vertexShader("Resources/Shaders/CL_Diffuse.hlsl", Shader::Type::VertexShader, "VSMain");
		Shader pixelShader("Resources/Shaders/CL_Diffuse.hlsl", Shader::Type::PixelShader, "PSMain");

		m_pDiffuseRS = std::make_unique<RootSignature>();
		m_pDiffuseRS->FinalizeFromShader("Diffuse", vertexShader, pGraphics->GetDevice());

		//Opaque
		m_pDiffusePSO = std::make_unique<GraphicsPipelineState>();
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
		m_pDiffuseTransparancyPSO = std::make_unique<GraphicsPipelineState>(*m_pDiffusePSO.get());
		m_pDiffuseTransparancyPSO->SetBlendMode(BlendMode::Alpha, false);
		m_pDiffuseTransparancyPSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
		m_pDiffuseTransparancyPSO->Finalize("Diffuse (Transparant)", m_pGraphics->GetDevice());
	}

	//Cluster debug rendering
	{
		Shader vertexShader("Resources/Shaders/CL_DebugDrawClusters.hlsl", Shader::Type::VertexShader, "VSMain");
		Shader geometryShader("Resources/Shaders/CL_DebugDrawClusters.hlsl", Shader::Type::GeometryShader, "GSMain");
		Shader pixelShader("Resources/Shaders/CL_DebugDrawClusters.hlsl", Shader::Type::PixelShader, "PSMain");

		m_pDebugClustersRS = std::make_unique<RootSignature>();
		m_pDebugClustersRS->FinalizeFromShader("Debug Clusters", vertexShader, m_pGraphics->GetDevice());

		m_pDebugClustersPSO = std::make_unique<GraphicsPipelineState>();
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