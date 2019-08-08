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

static constexpr int cClusterSize = 64;
static constexpr int cClusterCountZ = 32;

bool gUseAlternativeLightCulling = false;
bool gVisualizeClusters = false;

ClusteredForward::ClusteredForward(Graphics* pGraphics)
	: m_pGraphics(pGraphics)
{
	SetupResources(pGraphics);
	SetupPipelines(pGraphics);
}

void ClusteredForward::OnSwapchainCreated(int windowWidth, int windowHeight)
{
	m_ClusterCountX = (uint32)ceil((float)windowWidth / cClusterSize);
	m_ClusterCountY = (uint32)ceil((float)windowHeight / cClusterSize);

	struct AABB { Vector4 Min; Vector4 Max; };
	uint64 totalClusterCount = (uint64)m_ClusterCountX * m_ClusterCountY * cClusterCountZ;
	m_pAABBs->Create(m_pGraphics, sizeof(AABB), totalClusterCount, false);
	m_pAABBs->SetName("AABBs");
	DXGI_FORMAT bufferFormat = m_pGraphics->CheckTypedUAVSupport(DXGI_FORMAT_R8_UINT) ? DXGI_FORMAT_R8_UINT : DXGI_FORMAT_R32_UINT;
	m_pUniqueClusters->Create(m_pGraphics, bufferFormat, totalClusterCount, false);
	m_pUniqueClusters->SetName("Unique Clusters");
	m_pDebugCompactedClusters->Create(m_pGraphics, sizeof(uint32), totalClusterCount, false);
	m_pDebugCompactedClusters->SetName("Debug Compacted Clusters");
	m_pCompactedClusters->Create(m_pGraphics, sizeof(uint32), totalClusterCount, false);
	m_pCompactedClusters->SetName("Compacted Clusters");
	m_pLightIndexGrid->Create(m_pGraphics, sizeof(uint32), 32 * totalClusterCount);
	m_pLightIndexGrid->SetName("Light Index Grid");
	m_pLightGrid->Create(m_pGraphics, 2 * sizeof(uint32), totalClusterCount);
	m_pLightGrid->SetName("Light Grid");
	m_pDebugLightGrid->Create(m_pGraphics, 2 * sizeof(uint32), totalClusterCount);
	m_pDebugLightGrid->SetName("Debug Light Grid");

	float nearZ = 2.0f;
	float farZ = 500.0f;
	Matrix projection = XMMatrixPerspectiveFovLH(Math::ToRadians * 60, (float)windowWidth / windowHeight, nearZ, farZ);

	// Create AABBs
	{
		ComputeCommandContext* pContext = (ComputeCommandContext*)m_pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_COMPUTE);
		Profiler::Instance()->Begin("Create AABBs", pContext);

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
		constantBuffer.NearZ = nearZ;
		constantBuffer.FarZ = farZ;
		projection.Invert(constantBuffer.ProjectionInverse);
		constantBuffer.ClusterSize.x = cClusterSize;
		constantBuffer.ClusterSize.y = cClusterSize;
		constantBuffer.ClusterDimensions[0] = m_ClusterCountX;
		constantBuffer.ClusterDimensions[1] = m_ClusterCountY;
		constantBuffer.ClusterDimensions[2] = cClusterCountZ;

		pContext->SetComputeDynamicConstantBufferView(0, &constantBuffer, sizeof(ConstantBuffer));
		pContext->SetDynamicDescriptor(1, 0, m_pAABBs->GetUAV());

		pContext->Dispatch(m_ClusterCountX, m_ClusterCountY, cClusterCountZ);

		Profiler::Instance()->End(pContext);
		uint64 fence = pContext->Execute(true);
	}
}

void ClusteredForward::Execute(const ClusteredForwardInputResources& resources)
{
	Vector2 screenDimensions((float)m_pGraphics->GetWindowWidth(), (float)m_pGraphics->GetWindowHeight());
	float nearZ = 2.0f;
	float farZ = 500.0f;
	Matrix projection = XMMatrixPerspectiveFovLH(Math::ToRadians * 60, screenDimensions.x / screenDimensions.y, nearZ, farZ);

	float sliceMagicA = (float)cClusterCountZ / log(farZ / nearZ);
	float sliceMagicB = ((float)cClusterCountZ * log(nearZ)) / log(farZ / nearZ);

	//Mark Unique Clusters
	{
		GraphicsCommandContext* pContext = (GraphicsCommandContext*)m_pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
		Profiler::Instance()->Begin("Mark Clusters", pContext);

		pContext->InsertResourceBarrier(m_pUniqueClusters.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, false);
		pContext->InsertResourceBarrier(resources.pDepthPrepassBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);

		Profiler::Instance()->Begin("Update Data", pContext);
		std::vector<uint32> zero(m_ClusterCountX * m_ClusterCountY * cClusterCountZ);
		m_pUniqueClusters->SetData(pContext, zero.data(), sizeof(uint32) * zero.size());
		Profiler::Instance()->End(pContext);

		ClearValues values;
		values.ClearDepth = true;
		pContext->BeginRenderPass(nullptr, resources.pDepthPrepassBuffer, values, RenderPassAccess::DontCare_DontCare, RenderPassAccess::Clear_Store);

		pContext->SetGraphicsPipelineState(m_pMarkUniqueClustersOpaquePSO.get());
		pContext->SetGraphicsRootSignature(m_pMarkUniqueClustersRS.get());
		pContext->SetViewport(FloatRect(0, 0, screenDimensions.x, screenDimensions.y));
		pContext->SetScissorRect(FloatRect(0, 0, screenDimensions.x, screenDimensions.y));
		pContext->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		struct ConstantBuffer
		{
			Matrix WorldView;
			Matrix Projection;
			uint32 ClusterDimensions[4];
			float ClusterSize[2];
			float SliceMagicA;
			float SliceMagicB;
		} constantBuffer;

		constantBuffer.WorldView = m_pGraphics->GetViewMatrix();
		constantBuffer.Projection = projection;
		constantBuffer.SliceMagicA = sliceMagicA;
		constantBuffer.SliceMagicB = sliceMagicB;
		constantBuffer.ClusterDimensions[0] = m_ClusterCountX;
		constantBuffer.ClusterDimensions[1] = m_ClusterCountY;
		constantBuffer.ClusterDimensions[2] = cClusterCountZ;
		constantBuffer.ClusterDimensions[3] = 0;
		constantBuffer.ClusterSize[0] = cClusterSize;
		constantBuffer.ClusterSize[1] = cClusterSize;

		{
			Profiler::Instance()->Begin("Opaque", pContext);
			pContext->SetDynamicConstantBufferView(0, &constantBuffer, sizeof(ConstantBuffer));
			pContext->SetDynamicDescriptor(1, 0, m_pUniqueClusters->GetUAV());
			for (const Batch& b : *resources.pOpaqueBatches)
			{
				b.pMesh->Draw(pContext);
			}
			Profiler::Instance()->End(pContext);
		}

		{
			Profiler::Instance()->Begin("Transparant", pContext);
			pContext->SetGraphicsPipelineState(m_pMarkUniqueClustersTransparantPSO.get());
			for (const Batch& b : *resources.pTransparantBatches)
			{
				pContext->SetDynamicDescriptor(2, 0, b.pMaterial->pDiffuseTexture->GetSRV());
				b.pMesh->Draw(pContext);
			}
			Profiler::Instance()->End(pContext);
		}

		Profiler::Instance()->End(pContext);
		pContext->EndRenderPass();

		uint64 fence = pContext->Execute(false);

		//m_pGraphics->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE)->InsertWaitForFence(fence);
	}

	{
		ComputeCommandContext* pContext = (ComputeCommandContext*)m_pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
		// Compact Clusters
		{
			Profiler::Instance()->Begin("Compact Clusters", pContext);
			pContext->SetComputePipelineState(m_pCompactClustersPSO.get());
			pContext->SetComputeRootSignature(m_pCompactClustersRS.get());

			uint32 values[] = { 0,0,0,0 };
			pContext->ClearUavUInt(m_pCompactedClusters->GetCounter(), values);

			pContext->SetDynamicDescriptor(0, 0, m_pUniqueClusters->GetSRV());
			pContext->SetDynamicDescriptor(1, 0, m_pCompactedClusters->GetUAV());

			pContext->Dispatch((int)ceil(m_ClusterCountX * m_ClusterCountY * cClusterCountZ / 64.0f), 1, 1);

			Profiler::Instance()->End(pContext);
			pContext->ExecuteAndReset(false);
		}

		// Update Indirect Arguments
		{
			Profiler::Instance()->Begin("Update Indirect Arguments", pContext);

			pContext->InsertResourceBarrier(m_pIndirectArguments.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);

			pContext->SetComputePipelineState(m_pUpdateIndirectArgumentsPSO.get());
			pContext->SetComputeRootSignature(m_pUpdateIndirectArgumentsRS.get());

			pContext->SetDynamicDescriptor(0, 0, m_pCompactedClusters->GetCounter()->GetSRV());
			pContext->SetDynamicDescriptor(1, 0, m_pIndirectArguments->GetUAV());

			pContext->Dispatch(1, 1, 1);
			Profiler::Instance()->End(pContext);
			pContext->ExecuteAndReset(false);
		}

		if (gUseAlternativeLightCulling)
		{
			// Light Culling
			{
				Profiler::Instance()->Begin("Alternative Light Culling", pContext);
				pContext->SetComputePipelineState(m_pAlternativeLightCullingPSO.get());
				pContext->SetComputeRootSignature(m_pLightCullingRS.get());

				pContext->InsertResourceBarrier(m_pIndirectArguments.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, false);
				pContext->InsertResourceBarrier(m_pCompactedClusters.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, false);
				pContext->InsertResourceBarrier(m_pAABBs.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, true);

				Profiler::Instance()->Begin("Set Data", pContext);
				uint32 zero = 0;
				m_pLightIndexCounter->SetData(pContext, &zero, sizeof(uint32));
				std::vector<char> zeros(2 * sizeof(uint32) * m_ClusterCountX * m_ClusterCountY * cClusterCountZ);
				m_pLightGrid->SetData(pContext, zeros.data(), sizeof(char) * zeros.size());
				Profiler::Instance()->End(pContext);

				struct ConstantBuffer
				{
					Matrix View;
					uint32 ClusterDimensions[3];
					int LightCount;
				} constantBuffer;

				constantBuffer.View = m_pGraphics->GetViewMatrix();
				constantBuffer.ClusterDimensions[0] = m_ClusterCountX;
				constantBuffer.ClusterDimensions[1] = m_ClusterCountY;
				constantBuffer.ClusterDimensions[2] = cClusterCountZ;
				constantBuffer.LightCount = resources.pLightBuffer->GetElementCount();

				pContext->SetComputeDynamicConstantBufferView(0, &constantBuffer, sizeof(ConstantBuffer));

				pContext->SetDynamicDescriptor(1, 0, resources.pLightBuffer->GetSRV());
				pContext->SetDynamicDescriptor(1, 1, m_pAABBs->GetSRV());
				pContext->SetDynamicDescriptor(1, 2, m_pCompactedClusters->GetSRV());

				pContext->SetDynamicDescriptor(2, 0, m_pLightIndexCounter->GetUAV());
				pContext->SetDynamicDescriptor(2, 1, m_pLightIndexGrid->GetUAV());
				pContext->SetDynamicDescriptor(2, 2, m_pLightGrid->GetUAV());

				pContext->Dispatch((int)ceil((float)m_ClusterCountX / 4), (int)ceil((float)m_ClusterCountY / 4), (int)ceil((float)cClusterCountZ / 4));

				Profiler::Instance()->End(pContext);
				uint64 fence = pContext->Execute(false);

				//m_pGraphics->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT)->InsertWaitForFence(fence);
			}
		}
		else
		{
			// Light Culling
			{
				Profiler::Instance()->Begin("Light Culling", pContext);
				pContext->SetComputePipelineState(m_pLightCullingPSO.get());
				pContext->SetComputeRootSignature(m_pLightCullingRS.get());

				pContext->InsertResourceBarrier(m_pIndirectArguments.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, false);
				pContext->InsertResourceBarrier(m_pCompactedClusters.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, false);
				pContext->InsertResourceBarrier(m_pAABBs.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, false);
				pContext->InsertResourceBarrier(m_pLightGrid.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, false);
				pContext->InsertResourceBarrier(m_pLightIndexGrid.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);

				uint32 zero = 0;
				m_pLightIndexCounter->SetData(pContext, &zero, sizeof(uint32));

				struct ConstantBuffer
				{
					Matrix View;
					int LightCount;
				} constantBuffer;

				constantBuffer.View = m_pGraphics->GetViewMatrix();
				constantBuffer.LightCount = resources.pLightBuffer->GetElementCount();

				pContext->SetComputeDynamicConstantBufferView(0, &constantBuffer, sizeof(ConstantBuffer));

				pContext->SetDynamicDescriptor(1, 0, resources.pLightBuffer->GetSRV());
				pContext->SetDynamicDescriptor(1, 1, m_pAABBs->GetSRV());
				pContext->SetDynamicDescriptor(1, 2, m_pCompactedClusters->GetSRV());

				pContext->SetDynamicDescriptor(2, 0, m_pLightIndexCounter->GetUAV());
				pContext->SetDynamicDescriptor(2, 1, m_pLightIndexGrid->GetUAV());
				pContext->SetDynamicDescriptor(2, 2, m_pLightGrid->GetUAV());

				pContext->ExecuteIndirect(m_pLightCullingCommandSignature.Get(), m_pIndirectArguments.get());

				Profiler::Instance()->End(pContext);
				uint64 fence = pContext->Execute(false);

				//m_pGraphics->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT)->InsertWaitForFence(fence);
			}
		}
	}

	//Base Pass
	{
		GraphicsCommandContext* pContext = (GraphicsCommandContext*)(m_pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT));
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

		Matrix view = m_pGraphics->GetViewMatrix();
		frameData.View = view;
		frameData.Projection = projection;
		view.Invert(frameData.ViewInverse);
		frameData.ScreenDimensions = screenDimensions;
		frameData.NearZ = nearZ;
		frameData.FarZ = farZ;
		frameData.ClusterDimensions[0] = m_ClusterCountX;
		frameData.ClusterDimensions[1] = m_ClusterCountY;
		frameData.ClusterDimensions[2] = cClusterCountZ;
		frameData.ClusterDimensions[3] = 0;
		frameData.ClusterSize[0] = cClusterSize;
		frameData.ClusterSize[1] = cClusterSize;
		frameData.SliceMagicA = sliceMagicA;
		frameData.SliceMagicB = sliceMagicB;

		Profiler::Instance()->Begin("Lighting Pass", pContext);

		pContext->InsertResourceBarrier(m_pLightGrid.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, false);
		pContext->InsertResourceBarrier(m_pLightIndexGrid.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, false);
		pContext->InsertResourceBarrier(resources.pRenderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET, true);

		ClearValues values;
		values.ClearColor = true;
		pContext->BeginRenderPass(resources.pRenderTarget, resources.pDepthPrepassBuffer, values, RenderPassAccess::Clear_Store, RenderPassAccess::Load_DontCare);
		pContext->SetViewport(FloatRect(0, 0, (float)screenDimensions.x, (float)screenDimensions.y));
		pContext->SetScissorRect(FloatRect(0, 0, (float)screenDimensions.x, (float)screenDimensions.y));

		{
			Profiler::Instance()->Begin("Opaque", pContext);
			pContext->SetGraphicsPipelineState(m_pDiffusePSO.get());
			pContext->SetGraphicsRootSignature(m_pDiffuseRS.get());

			pContext->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			pContext->SetDynamicConstantBufferView(1, &frameData, sizeof(PerFrameData));
			pContext->SetDynamicDescriptor(3, 0, m_pLightGrid->GetSRV());
			pContext->SetDynamicDescriptor(3, 1, m_pLightIndexGrid->GetSRV());
			pContext->SetDynamicDescriptor(3, 2, resources.pLightBuffer->GetSRV());
			pContext->SetDynamicDescriptor(4, 0, m_pHeatMapTexture->GetSRV());

			for (const Batch& b : *resources.pOpaqueBatches)
			{
				objectData.World = XMMatrixIdentity();
				pContext->SetDynamicConstantBufferView(0, &objectData, sizeof(PerObjectData));
				pContext->SetDynamicDescriptor(2, 0, b.pMaterial->pDiffuseTexture->GetSRV());
				pContext->SetDynamicDescriptor(2, 1, b.pMaterial->pNormalTexture->GetSRV());
				pContext->SetDynamicDescriptor(2, 2, b.pMaterial->pSpecularTexture->GetSRV());
				b.pMesh->Draw(pContext);
			}
			Profiler::Instance()->End(pContext);
		}

		{
			Profiler::Instance()->Begin("Transparant", pContext);
			pContext->SetGraphicsPipelineState(m_pDiffuseTransparancyPSO.get());

			for (const Batch& b : *resources.pTransparantBatches)
			{
				objectData.World = XMMatrixIdentity();
				pContext->SetDynamicConstantBufferView(0, &objectData, sizeof(PerObjectData));
				pContext->SetDynamicDescriptor(2, 0, b.pMaterial->pDiffuseTexture->GetSRV());
				pContext->SetDynamicDescriptor(2, 1, b.pMaterial->pNormalTexture->GetSRV());
				pContext->SetDynamicDescriptor(2, 2, b.pMaterial->pSpecularTexture->GetSRV());
				b.pMesh->Draw(pContext);
			}
			Profiler::Instance()->End(pContext);
		}

		pContext->EndRenderPass();
		Profiler::Instance()->End(pContext);
		pContext->Execute(false);
	}

	if (gVisualizeClusters)
	{
		GraphicsCommandContext* pContext = (GraphicsCommandContext*)(m_pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT));
		Profiler::Instance()->Begin("Cluster Visualization", pContext);

		if (m_DidCopyDebugClusterData == false)
		{
			pContext->CopyResource(m_pCompactedClusters.get(), m_pDebugCompactedClusters.get());
			pContext->CopyResource(m_pLightGrid.get(), m_pDebugLightGrid.get());
			m_DebugClustersViewMatrix = m_pGraphics->GetViewMatrix();
			m_DebugClustersViewMatrix.Invert(m_DebugClustersViewMatrix);
			pContext->ExecuteAndReset(true);
			m_DidCopyDebugClusterData = true;
		}

		ClearValues values;
		pContext->BeginRenderPass(resources.pRenderTarget, resources.pDepthPrepassBuffer, values, RenderPassAccess::Load_Store, RenderPassAccess::Load_DontCare);

		pContext->SetGraphicsPipelineState(m_pDebugClustersPSO.get());
		pContext->SetGraphicsRootSignature(m_pDebugClustersRS.get());

		pContext->SetViewport(FloatRect(0, 0, (float)screenDimensions.x, (float)screenDimensions.y));
		pContext->SetScissorRect(FloatRect(0, 0, (float)screenDimensions.x, (float)screenDimensions.y));
		pContext->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
		
		Matrix p = m_DebugClustersViewMatrix * m_pGraphics->GetViewMatrix() * projection;

		pContext->SetDynamicConstantBufferView(0, &p, sizeof(Matrix));
		pContext->SetDynamicDescriptor(1, 0, m_pAABBs->GetSRV());
		pContext->SetDynamicDescriptor(1, 1, m_pDebugCompactedClusters->GetSRV());
		pContext->SetDynamicDescriptor(1, 2, m_pDebugLightGrid->GetSRV());
		pContext->SetDynamicDescriptor(1, 3, m_pHeatMapTexture->GetSRV());
		pContext->Draw(0, m_ClusterCountX* m_ClusterCountY* cClusterCountZ);

		pContext->EndRenderPass();
		Profiler::Instance()->End(pContext);

		pContext->Execute(false);
	}
	else
	{
		m_DidCopyDebugClusterData = false;
	}
}

void ClusteredForward::SetupResources(Graphics* pGraphics)
{
	m_pAABBs = std::make_unique<StructuredBuffer>(pGraphics);
	m_pUniqueClusters = std::make_unique<TypedBuffer>(pGraphics);
	m_pCompactedClusters = std::make_unique<StructuredBuffer>(pGraphics);
	m_pDebugCompactedClusters = std::make_unique<StructuredBuffer>(pGraphics);
	m_pIndirectArguments = std::make_unique<ByteAddressBuffer>(pGraphics);
	m_pIndirectArguments->Create(m_pGraphics, sizeof(uint32), 3, false);
	m_pLightIndexCounter = std::make_unique<StructuredBuffer>(pGraphics);
	m_pLightIndexCounter->Create(pGraphics, sizeof(uint32), 1);
	m_pLightIndexGrid = std::make_unique<StructuredBuffer>(pGraphics);
	m_pLightGrid = std::make_unique<StructuredBuffer>(pGraphics);
	m_pDebugLightGrid = std::make_unique<StructuredBuffer>(pGraphics);

	CopyCommandContext* pContext = (CopyCommandContext*)pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_COPY);
	m_pHeatMapTexture = std::make_unique<Texture2D>();
	m_pHeatMapTexture->Create(pGraphics, pContext, "Resources/Textures/Heatmap.png", TextureUsage::ShaderResource);
	m_pHeatMapTexture->SetName("Heatmap texture");
	pContext->Execute(true);

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
		std::vector<D3D12_INPUT_ELEMENT_DESC> inputElements = {
			D3D12_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};

		Shader vertexShader("Resources/Shaders/CL_MarkUniqueClusters.hlsl", Shader::Type::VertexShader, "MarkClusters_VS");
		Shader pixelShaderOpaque("Resources/Shaders/CL_MarkUniqueClusters.hlsl", Shader::Type::PixelShader, "MarkClusters_PS");
		Shader pixelShaderTransparant("Resources/Shaders/CL_MarkUniqueClusters.hlsl", Shader::Type::PixelShader, "MarkClusters_PS", {"ALPHA_BLEND"});

		m_pMarkUniqueClustersRS = std::make_unique<RootSignature>();
		m_pMarkUniqueClustersRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
		m_pMarkUniqueClustersRS->SetDescriptorTableSimple(1, 1, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, D3D12_SHADER_VISIBILITY_ALL);
		m_pMarkUniqueClustersRS->SetDescriptorTableSimple(2, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, D3D12_SHADER_VISIBILITY_ALL);

		D3D12_SAMPLER_DESC samplerDesc = {};
		samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
		m_pMarkUniqueClustersRS->AddStaticSampler(0, samplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);

		m_pMarkUniqueClustersRS->Finalize("Mark Unique Clusters", pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		m_pMarkUniqueClustersOpaquePSO = std::make_unique<GraphicsPipelineState>();
		m_pMarkUniqueClustersOpaquePSO->SetDepthTest(D3D12_COMPARISON_FUNC_LESS_EQUAL);
		m_pMarkUniqueClustersOpaquePSO->SetRootSignature(m_pMarkUniqueClustersRS->GetRootSignature());
		m_pMarkUniqueClustersOpaquePSO->SetBlendMode(BlendMode::Replace, false);
		m_pMarkUniqueClustersOpaquePSO->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
		m_pMarkUniqueClustersOpaquePSO->SetPixelShader(pixelShaderOpaque.GetByteCode(), pixelShaderOpaque.GetByteCodeSize());
		m_pMarkUniqueClustersOpaquePSO->SetInputLayout(inputElements.data(), (uint32)inputElements.size());
		m_pMarkUniqueClustersOpaquePSO->SetRenderTargetFormats(nullptr, 0, Graphics::DEPTH_STENCIL_FORMAT, m_pGraphics->GetMultiSampleCount(), m_pGraphics->GetMultiSampleQualityLevel(m_pGraphics->GetMultiSampleCount()));
		m_pMarkUniqueClustersOpaquePSO->Finalize("Mark Unique Clusters", m_pGraphics->GetDevice());

		m_pMarkUniqueClustersTransparantPSO = std::make_unique<GraphicsPipelineState>(*m_pMarkUniqueClustersOpaquePSO);
		m_pMarkUniqueClustersTransparantPSO->SetBlendMode(BlendMode::Alpha, false);
		m_pMarkUniqueClustersTransparantPSO->SetPixelShader(pixelShaderTransparant.GetByteCode(), pixelShaderTransparant.GetByteCodeSize());
		m_pMarkUniqueClustersTransparantPSO->SetDepthWrite(false);
		m_pMarkUniqueClustersTransparantPSO->Finalize("Mark Unique Clusters", m_pGraphics->GetDevice());
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

	//Prepare Indirect Dispatch Buffer
	{
		Shader computeShader = Shader("Resources/Shaders/CL_UpdateIndirectArguments.hlsl", Shader::Type::ComputeShader, "UpdateIndirectArguments");

		m_pUpdateIndirectArgumentsRS = std::make_unique<RootSignature>();
		m_pUpdateIndirectArgumentsRS->SetDescriptorTableSimple(0, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, D3D12_SHADER_VISIBILITY_ALL);
		m_pUpdateIndirectArgumentsRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, D3D12_SHADER_VISIBILITY_ALL);
		m_pUpdateIndirectArgumentsRS->Finalize("Update Indirect Dispatch Buffer", pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_NONE);

		m_pUpdateIndirectArgumentsPSO = std::make_unique<ComputePipelineState>();
		m_pUpdateIndirectArgumentsPSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pUpdateIndirectArgumentsPSO->SetRootSignature(m_pUpdateIndirectArgumentsRS->GetRootSignature());
		m_pUpdateIndirectArgumentsPSO->Finalize("Update Indirect Dispatch Buffer", pGraphics->GetDevice());
	}

	//Light Culling
	{
		Shader computeShader = Shader("Resources/Shaders/CL_LightCulling.hlsl", Shader::Type::ComputeShader, "LightCulling");

		m_pLightCullingRS = std::make_unique<RootSignature>();
		m_pLightCullingRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
		m_pLightCullingRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, D3D12_SHADER_VISIBILITY_ALL);
		m_pLightCullingRS->SetDescriptorTableSimple(2, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 3, D3D12_SHADER_VISIBILITY_ALL);
		m_pLightCullingRS->Finalize("Light Culling", pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_NONE);

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

	//Alternative Light Culling
	{
		Shader computeShader = Shader("Resources/Shaders/CL_LightCullingUnreal.hlsl", Shader::Type::ComputeShader, "LightCulling");

		m_pAlternativeLightCullingPSO = std::make_unique<ComputePipelineState>();
		m_pAlternativeLightCullingPSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pAlternativeLightCullingPSO->SetRootSignature(m_pLightCullingRS->GetRootSignature());
		m_pAlternativeLightCullingPSO->Finalize("Light Culling", pGraphics->GetDevice());
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
		m_pDiffuseRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
		m_pDiffuseRS->SetConstantBufferView(1, 1, D3D12_SHADER_VISIBILITY_ALL);
		m_pDiffuseRS->SetDescriptorTableSimple(2, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, D3D12_SHADER_VISIBILITY_ALL);
		m_pDiffuseRS->SetDescriptorTableSimple(3, 3, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, D3D12_SHADER_VISIBILITY_PIXEL);
		m_pDiffuseRS->SetDescriptorTableSimple(4, 6, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, D3D12_SHADER_VISIBILITY_ALL);

		D3D12_SAMPLER_DESC samplerDesc = {};
		samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
		m_pDiffuseRS->AddStaticSampler(0, samplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);

		samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
		m_pDiffuseRS->AddStaticSampler(1, samplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);

		m_pDiffuseRS->Finalize("Diffuse", pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		//Opaque
		m_pDiffusePSO = std::make_unique<GraphicsPipelineState>();
		m_pDiffusePSO->SetRootSignature(m_pDiffuseRS->GetRootSignature());
		m_pDiffusePSO->SetBlendMode(BlendMode::Replace, false);
		m_pDiffusePSO->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
		m_pDiffusePSO->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
		m_pDiffusePSO->SetInputLayout(inputElements, 5);
		m_pDiffusePSO->SetDepthTest(D3D12_COMPARISON_FUNC_EQUAL);
		m_pDiffusePSO->SetDepthWrite(false);
		m_pDiffusePSO->SetRenderTargetFormat(Graphics::RENDER_TARGET_FORMAT, Graphics::DEPTH_STENCIL_FORMAT, m_pGraphics->GetMultiSampleCount(), m_pGraphics->GetMultiSampleQualityLevel(m_pGraphics->GetMultiSampleCount()));
		m_pDiffusePSO->Finalize("Diffuse (Opaque)", m_pGraphics->GetDevice());

		//Transparant
		m_pDiffuseTransparancyPSO = std::make_unique<GraphicsPipelineState>(*m_pDiffusePSO.get());
		m_pDiffuseTransparancyPSO->SetBlendMode(BlendMode::Alpha, false);
		m_pDiffuseTransparancyPSO->SetDepthTest(D3D12_COMPARISON_FUNC_LESS_EQUAL);
		m_pDiffuseTransparancyPSO->Finalize("Diffuse (Transparant)", m_pGraphics->GetDevice());
	}

	//Cluster debug rendering
	{
		Shader vertexShader("Resources/Shaders/CL_DebugDrawClusters.hlsl", Shader::Type::VertexShader, "VSMain");
		Shader geometryShader("Resources/Shaders/CL_DebugDrawClusters.hlsl", Shader::Type::GeometryShader, "GSMain");
		Shader pixelShader("Resources/Shaders/CL_DebugDrawClusters.hlsl", Shader::Type::PixelShader, "PSMain");

		m_pDebugClustersRS = std::make_unique<RootSignature>();
		m_pDebugClustersRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_GEOMETRY);
		m_pDebugClustersRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, D3D12_SHADER_VISIBILITY_VERTEX);
		m_pDebugClustersRS->Finalize("Debug Clusters", m_pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS | D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS);

		m_pDebugClustersPSO = std::make_unique<GraphicsPipelineState>();
		m_pDebugClustersPSO->SetDepthTest(D3D12_COMPARISON_FUNC_LESS_EQUAL);
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