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

static constexpr int cClusterDimensionsX = 16;
static constexpr int cClusterDimensionsY = 9;
static constexpr int cClusterDimensionsZ = 24;

ClusteredForward::ClusteredForward(Graphics* pGraphics)
	: m_pGraphics(pGraphics)
{
	SetupResources(pGraphics);
	SetupPipelines(pGraphics);
}

void ClusteredForward::OnSwapchainCreated(int windowWidth, int windowHeight)
{
	struct AABB { Vector4 Min; Vector4 Max; };
	m_pAABBs->Create(m_pGraphics, sizeof(AABB), cClusterDimensionsX * cClusterDimensionsY * cClusterDimensionsZ, false);
	m_pAABBs->SetName("AABBs");
	m_pUniqueClusters->Create(m_pGraphics, sizeof(uint32), cClusterDimensionsX * cClusterDimensionsY * cClusterDimensionsZ, false);
	m_pUniqueClusters->SetName("UniqueClusters");
	m_pActiveClusters->Create(m_pGraphics, sizeof(uint32), cClusterDimensionsX* cClusterDimensionsY* cClusterDimensionsZ, false);
	m_pActiveClusters->SetName("ActiveClusters");
	m_pDebugTexture->Create(m_pGraphics, windowWidth, windowHeight, Graphics::RENDER_TARGET_FORMAT, TextureUsage::RenderTarget, m_pGraphics->GetMultiSampleCount());
}

void ClusteredForward::Execute(const ClusteredForwardInputResources& resources)
{
	Vector2 screenDimensions((float)m_pGraphics->GetWindowWidth(), (float)m_pGraphics->GetWindowHeight());
	float nearZ = 0.1f;
	float farZ = 1000.0f;
	Matrix projection = XMMatrixPerspectiveFovLH(XM_PIDIV4, screenDimensions.x / screenDimensions.y, nearZ, farZ);
	
	// Create AABBs
	{
		ComputeCommandContext* pContext = (ComputeCommandContext*)m_pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_COMPUTE);
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

		constantBuffer.ScreenDimensions = screenDimensions;
		constantBuffer.NearZ = nearZ;
		constantBuffer.FarZ = farZ;
		projection.Invert(constantBuffer.ProjectionInverse);
		constantBuffer.ClusterSize.x = screenDimensions.x / cClusterDimensionsX;
		constantBuffer.ClusterSize.y = screenDimensions.y / cClusterDimensionsY;
		constantBuffer.ClusterDimensions[0] = cClusterDimensionsX;
		constantBuffer.ClusterDimensions[1] = cClusterDimensionsY;
		constantBuffer.ClusterDimensions[2] = cClusterDimensionsZ;

		pContext->SetComputeDynamicConstantBufferView(0, &constantBuffer, sizeof(ConstantBuffer));
		pContext->SetDynamicDescriptor(1, 0, m_pAABBs->GetUAV());

		pContext->Dispatch(cClusterDimensionsX, cClusterDimensionsY, cClusterDimensionsZ);

		uint64 fence = pContext->Execute(false);
		m_pGraphics->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT)->InsertWaitForFence(fence);
	}

	//Mark Unique Clusters
	{
		GraphicsCommandContext* pContext = (GraphicsCommandContext*)m_pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
		
		pContext->InsertResourceBarrier(m_pUniqueClusters.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, false);
		pContext->InsertResourceBarrier(resources.pDepthPrepassBuffer, D3D12_RESOURCE_STATE_DEPTH_READ, false);
		pContext->InsertResourceBarrier(m_pDebugTexture.get(), D3D12_RESOURCE_STATE_RENDER_TARGET, true);

		std::vector<uint32> zero(cClusterDimensionsX * cClusterDimensionsY * cClusterDimensionsZ);
		m_pUniqueClusters->SetData(pContext, zero.data(), zero.size());

		pContext->SetGraphicsPipelineState(m_pMarkUniqueClustersPSO.get());
		pContext->SetGraphicsRootSignature(m_pMarkUniqueClustersRS.get());
		pContext->SetViewport(FloatRect(0, 0, screenDimensions.x, screenDimensions.y));
		pContext->SetScissorRect(FloatRect(0, 0, screenDimensions.x, screenDimensions.y));
		//pContext->SetRenderTargets(nullptr, resources.pDepthPrepassBuffer->GetDSV());
		pContext->SetRenderTarget(resources.pRenderTarget->GetRTV(), resources.pDepthPrepassBuffer->GetDSV());
		pContext->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		struct ConstantBuffer
		{
			Matrix WorldView;
			Matrix Projection;
			Vector2 ScreenDimensions;
			float NearZ;
			float FarZ;
			uint32 ClusterDimensions[4];
			float ClusterSize[2];
		} constantBuffer;

		constantBuffer.WorldView = m_pGraphics->GetViewMatrix();
		constantBuffer.Projection = projection;
		constantBuffer.ScreenDimensions = screenDimensions;
		constantBuffer.NearZ = nearZ;
		constantBuffer.FarZ = farZ;
		constantBuffer.ClusterDimensions[0] = cClusterDimensionsX;
		constantBuffer.ClusterDimensions[1] = cClusterDimensionsY;
		constantBuffer.ClusterDimensions[2] = cClusterDimensionsZ;
		constantBuffer.ClusterDimensions[3] = 0;
		constantBuffer.ClusterSize[0] = screenDimensions.x / cClusterDimensionsX;
		constantBuffer.ClusterSize[1] = screenDimensions.y / cClusterDimensionsY;

		pContext->SetDynamicConstantBufferView(0, &constantBuffer, sizeof(ConstantBuffer));
		pContext->SetDynamicDescriptor(1, 0, m_pUniqueClusters->GetUAV());
		for (const Batch& b : *resources.pOpaqueBatches)
		{
			b.pMesh->Draw(pContext);
		}

		uint64 fence = pContext->Execute(false);
		m_pGraphics->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE)->InsertWaitForFence(fence);
	}

	// Compact Clusters
	{
		ComputeCommandContext* pContext = (ComputeCommandContext*)m_pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_COMPUTE);
		pContext->SetComputePipelineState(m_pCompactClustersPSO.get());
		pContext->SetComputeRootSignature(m_pCompactClustersRS.get());

		std::vector<uint32> zero(1);
		m_pActiveClusters->GetCounter()->SetData(pContext, zero.data(), zero.size());

		pContext->SetDynamicDescriptor(0, 0, m_pUniqueClusters->GetSRV());
		pContext->SetDynamicDescriptor(1, 0, m_pActiveClusters->GetUAV());

		pContext->Dispatch(cClusterDimensionsX * cClusterDimensionsY * cClusterDimensionsZ / 64, 0, 0);

		pContext->Execute(false);
	}
}

void ClusteredForward::SetupResources(Graphics* pGraphics)
{
	m_pAABBs = std::make_unique<StructuredBuffer>(pGraphics);
	m_pUniqueClusters = std::make_unique<StructuredBuffer>(pGraphics);
	m_pActiveClusters = std::make_unique<StructuredBuffer>(pGraphics);
	m_pDebugTexture = std::make_unique<Texture2D>();
}

void ClusteredForward::SetupPipelines(Graphics* pGraphics)
{
	//AABB
	{
		Shader computeShader = Shader("Resources/Shaders/CL_GenerateAABBs.hlsl", Shader::Type::ComputeShader, "GenerateAABBs");

		m_pCreateAabbRS = std::make_unique<RootSignature>();
		m_pCreateAabbRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
		m_pCreateAabbRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, D3D12_SHADER_VISIBILITY_ALL);
		m_pCreateAabbRS->Finalize("Create AABB", pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_NONE);

		m_pCreateAabbPSO = std::make_unique<ComputePipelineState>();
		m_pCreateAabbPSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pCreateAabbPSO->SetRootSignature(m_pCreateAabbRS->GetRootSignature());
		m_pCreateAabbPSO->Finalize("Create AABB", pGraphics->GetDevice());
	}

	//Mark Clusters
	{
		D3D12_INPUT_ELEMENT_DESC opaqueInputElements[] = {
		D3D12_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};

		Shader vertexShader("Resources/Shaders/CL_MarkUniqueClusters.hlsl", Shader::Type::VertexShader, "MarkClusters_VS");
		Shader pixelShader("Resources/Shaders/CL_MarkUniqueClusters.hlsl", Shader::Type::PixelShader, "MarkClusters_PS");

		m_pMarkUniqueClustersRS = std::make_unique<RootSignature>();
		m_pMarkUniqueClustersRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
		m_pMarkUniqueClustersRS->SetDescriptorTableSimple(1, 1, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, D3D12_SHADER_VISIBILITY_ALL);
		m_pMarkUniqueClustersRS->Finalize("Mark Unique Clusters", pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		m_pMarkUniqueClustersPSO = std::make_unique<GraphicsPipelineState>();
		m_pMarkUniqueClustersPSO->SetRootSignature(m_pMarkUniqueClustersRS->GetRootSignature());
		m_pMarkUniqueClustersPSO->SetBlendMode(BlendMode::Replace, false);
		m_pMarkUniqueClustersPSO->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
		m_pMarkUniqueClustersPSO->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
		m_pMarkUniqueClustersPSO->SetInputLayout(opaqueInputElements, 1);
		m_pMarkUniqueClustersPSO->SetRenderTargetFormat(Graphics::RENDER_TARGET_FORMAT, Graphics::DEPTH_STENCIL_FORMAT, m_pGraphics->GetMultiSampleCount(), m_pGraphics->GetMultiSampleQualityLevel(m_pGraphics->GetMultiSampleCount()));
		m_pMarkUniqueClustersPSO->SetDepthWrite(false);
		m_pMarkUniqueClustersPSO->SetDepthEnabled(false);
		m_pMarkUniqueClustersPSO->Finalize("Mark Unique Clusters", m_pGraphics->GetDevice());
	}

	//Compact Clusters
	{
		Shader computeShader = Shader("Resources/Shaders/CL_CompactClusters.hlsl", Shader::Type::ComputeShader, "CompactClusters");

		m_pCompactClustersRS = std::make_unique<RootSignature>();
		m_pCompactClustersRS->SetDescriptorTableSimple(0, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, D3D12_SHADER_VISIBILITY_ALL);
		m_pCompactClustersRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, D3D12_SHADER_VISIBILITY_ALL);
		m_pCompactClustersRS->Finalize("Compact Clusters", pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_NONE);

		m_pCompactClustersPSO = std::make_unique<ComputePipelineState>();
		m_pCompactClustersPSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pCompactClustersPSO->SetRootSignature(m_pCompactClustersRS->GetRootSignature());
		m_pCompactClustersPSO->Finalize("Compact Clusters", pGraphics->GetDevice());
	}
}
