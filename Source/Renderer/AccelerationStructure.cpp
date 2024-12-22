#include "stdafx.h"
#include "AccelerationStructure.h"
#include "Core/Profiler.h"
#include "Core/ConsoleVariables.h"
#include "RHI/Device.h"
#include "RHI/CommandContext.h"
#include "RHI/Buffer.h"
#include "RHI/PipelineState.h"
#include "RHI/RootSignature.h"
#include "Renderer/Mesh.h"
#include "Renderer/Renderer.h"
#include "Renderer/RenderTypes.h"

static constexpr uint32 sMaxNumBLASVerticesPerFrame = 100'000;
static constexpr uint32 sMaxNumCompactionsPerFrame = 32;

AccelerationStructure::AccelerationStructure() = default;
AccelerationStructure::~AccelerationStructure() = default;

void AccelerationStructure::Init(GraphicsDevice* pDevice)
{
	m_pUpdateTLASPSO = pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "UpdateTLAS.hlsl", "UpdateTLASCS");
}

void AccelerationStructure::Build(CommandContext& context, const Buffer* pInstancesBuffer, Span<const Batch> batches)
{
	PROFILE_CPU_SCOPE();

	GraphicsDevice* pDevice = context.GetParent();
	if (pDevice->GetCapabilities().SupportsRaytracing())
	{
		PROFILE_GPU_SCOPE(context.GetCommandList(), "Build Acceleration Structures");

		ID3D12GraphicsCommandListX* pCmd = context.GetCommandList();

		uint32 numBLASBuiltVertices = 0;
		uint32 numBuiltBLAS = 0;

		struct BLASInstance
		{
			uint64 GPUAddress;
			uint32 WorldMatrix;
			uint32 Flags			: 8;
			uint32 InstanceMask		: 8;
		};
		Array<BLASInstance> blasInstances;
		blasInstances.reserve(batches.GetSize());

		for (const Batch& batch : batches)
		{
			Mesh* pMesh = const_cast<Mesh*>(batch.pMesh);

			if ((!pMesh->pBLAS || pMesh->IsAnimated()) && numBLASBuiltVertices < sMaxNumBLASVerticesPerFrame)
			{
				numBLASBuiltVertices += pMesh->PositionStreamLocation.Elements;
				++numBuiltBLAS;

				D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc{};
				geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
				geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
				geometryDesc.Triangles.IndexBuffer = pMesh->IndicesLocation.Location;
				geometryDesc.Triangles.IndexCount = pMesh->IndicesLocation.Elements;
				geometryDesc.Triangles.IndexFormat = D3D::ConvertFormat(pMesh->IndicesLocation.Format);
				geometryDesc.Triangles.Transform3x4 = 0;
				geometryDesc.Triangles.VertexBuffer.StartAddress = pMesh->SkinnedPositionStreamLocation.IsValid() ? pMesh->SkinnedPositionStreamLocation.Location : pMesh->PositionStreamLocation.Location;
				geometryDesc.Triangles.VertexBuffer.StrideInBytes = pMesh->PositionStreamLocation.Stride;
				geometryDesc.Triangles.VertexCount = pMesh->PositionStreamLocation.Elements;
				geometryDesc.Triangles.VertexFormat = D3D::ConvertFormat(pMesh->PositionsFormat);

				D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS prebuildInfo{};
				prebuildInfo.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
				
				bool doUpdate = false;
				if (pMesh->IsAnimated())
				{
					prebuildInfo.Flags =
						D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD
						| D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;

					if (pMesh->pBLAS)
					{
						prebuildInfo.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
						doUpdate = true;
					}
				}
				else
				{
					prebuildInfo.Flags =
						D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE
						| D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION;
				}

				prebuildInfo.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
				prebuildInfo.NumDescs = 1;
				prebuildInfo.pGeometryDescs = &geometryDesc;

				D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
				pDevice->GetDevice()->GetRaytracingAccelerationStructurePrebuildInfo(&prebuildInfo, &info);

				Ref<Buffer> pBLASScratch = pMesh->pBLASScratch;
				Ref<Buffer> pBLAS = pMesh->pBLAS;

				if (!pBLAS)
				{
					pBLAS = pDevice->CreateBuffer(BufferDesc::CreateBLAS(Math::AlignUp<uint64>(info.ResultDataMaxSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT)), "BLAS.Buffer");
					pMesh->pBLAS = pBLAS;
				}

				if (!pBLASScratch)
				{
					pBLASScratch = pDevice->CreateBuffer(BufferDesc::CreateByteAddress(Math::AlignUp<uint64>(info.ScratchDataSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT), BufferFlag::UnorderedAccess | BufferFlag::NoBindless), "BLAS.ScratchBuffer");
					pMesh->pBLASScratch = pBLASScratch;
				}

				D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc{};
				asDesc.Inputs = prebuildInfo;
				asDesc.DestAccelerationStructureData = pBLAS->GetGpuHandle();
				asDesc.ScratchAccelerationStructureData = pBLASScratch->GetGpuHandle();
				asDesc.SourceAccelerationStructureData = doUpdate ? pBLAS->GetGpuHandle() : 0;	// Allowed to be in-place

				pCmd->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);

				if (!pMesh->IsAnimated())
					m_QueuedRequests.push_back(&pMesh->pBLAS);
			}

			if (pMesh->pBLAS)
			{
				BLASInstance& blasInstance = blasInstances.emplace_back();
				blasInstance.GPUAddress		= pMesh->pBLAS->GetGpuHandle();
				blasInstance.Flags			= D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
				blasInstance.WorldMatrix	= batch.InstanceID;
				blasInstance.InstanceMask	= 0xFF;

				if (batch.WorldMatrix.Determinant() < 0)
					blasInstance.Flags |= D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE;
			}
		}

		if (numBuiltBLAS > 0)
		{
			//E_LOG(Info, "Built %d BLAS instances. %d vertices", numBuiltBLAS, numBLASBuiltVertices);
		}

		{
			PROFILE_GPU_SCOPE(context.GetCommandList(), "BLAS Compaction");
			//ProcessCompaction(context);
		}

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
		if(!blasInstances.empty() || !m_pTLAS)
		{
			PROFILE_GPU_SCOPE(context.GetCommandList(), "TLAS Data Generation");

			D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS prebuildInfo{};
			prebuildInfo.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
			prebuildInfo.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
			prebuildInfo.Flags = buildFlags;
			prebuildInfo.NumDescs = (uint32)blasInstances.size();

			D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
			pDevice->GetDevice()->GetRaytracingAccelerationStructurePrebuildInfo(&prebuildInfo, &info);

			if (!m_pTLAS || m_pTLAS->GetSize() < info.ResultDataMaxSizeInBytes)
			{
				m_pScratch = pDevice->CreateBuffer(BufferDesc::CreateByteAddress(Math::AlignUp<uint64>(info.ScratchDataSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT), BufferFlag::UnorderedAccess), "TLAS.ScratchBuffer");
				m_pTLAS = pDevice->CreateBuffer(BufferDesc::CreateTLAS(Math::AlignUp<uint64>(info.ResultDataMaxSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT)), "TLAS.Buffer");
			}

			uint32 numInstances = Math::AlignUp(Math::Max(1u, (uint32)blasInstances.size()), 128u);
			if (!m_pBLASInstancesSourceBuffer || m_pBLASInstancesSourceBuffer->GetNumElements() < numInstances)
			{
				m_pBLASInstancesSourceBuffer = pDevice->CreateBuffer(BufferDesc::CreateStructured(numInstances, sizeof(D3D12_RAYTRACING_INSTANCE_DESC), BufferFlag::ShaderResource), "TLAS.BLASInstanceSourceDescs");
				m_pBLASInstancesTargetBuffer = pDevice->CreateBuffer(BufferDesc::CreateStructured(numInstances, sizeof(D3D12_RAYTRACING_INSTANCE_DESC), BufferFlag::UnorderedAccess), "TLAS.BLASInstanceTargetDescs");
			}

			if (!blasInstances.empty())
			{
				context.InsertResourceBarrier(m_pBLASInstancesSourceBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);

				ScratchAllocation alloc = context.AllocateScratch(sizeof(BLASInstance) * blasInstances.size());
				memcpy(alloc.pMappedMemory, blasInstances.data(), sizeof(BLASInstance)* blasInstances.size());
				context.CopyBuffer(alloc.pBackingResource, m_pBLASInstancesSourceBuffer, alloc.Size, alloc.Offset, 0);

				context.InsertResourceBarrier(m_pBLASInstancesSourceBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				context.InsertResourceBarrier(m_pBLASInstancesTargetBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

				context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
				context.SetPipelineState(m_pUpdateTLASPSO);
				context.BindRootCBV(BindingSlot::PerInstance, (uint32)blasInstances.size());
				context.BindResources(BindingSlot::UAV, m_pBLASInstancesTargetBuffer->GetUAV());
				context.BindResources(BindingSlot::SRV,
					{
						pInstancesBuffer->GetSRV(),
						m_pBLASInstancesSourceBuffer->GetSRV(),
					});
				context.Dispatch(ComputeUtils::GetNumThreadGroups((uint32)blasInstances.size(), 32));
			}
		}

		{
			PROFILE_GPU_SCOPE(context.GetCommandList(), "Build TLAS");

			D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc{};
			asDesc.DestAccelerationStructureData = m_pTLAS->GetGpuHandle();
			asDesc.ScratchAccelerationStructureData = m_pScratch->GetGpuHandle();
			asDesc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
			asDesc.Inputs.Flags = buildFlags;
			asDesc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
			asDesc.Inputs.InstanceDescs = m_pBLASInstancesTargetBuffer->GetGpuHandle();
			asDesc.Inputs.NumDescs = (uint32)blasInstances.size();
			asDesc.SourceAccelerationStructureData = 0;

			context.InsertResourceBarrier(m_pBLASInstancesTargetBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.FlushResourceBarriers();
			pCmd->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);
			context.InsertUAVBarrier(m_pTLAS);
		}
	}
}

ShaderResourceView* AccelerationStructure::GetSRV() const
{
	if (m_pTLAS)
	{
		return m_pTLAS->GetSRV();
	}
	return nullptr;
}

void AccelerationStructure::ProcessCompaction(CommandContext& context)
{
	if (!m_ActiveRequests.empty())
	{
		if (!m_PostBuildInfoFence.IsComplete())
		{
			return;
		}

		const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE_DESC* pPostCompactSizes = static_cast<D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE_DESC*>(m_pPostBuildInfoReadbackBuffer->GetMappedData());
		for (Ref<Buffer>* pSourceBLAS: m_ActiveRequests)
		{
			gAssert(pPostCompactSizes->CompactedSizeInBytes > 0);
			Ref<Buffer> pTargetBLAS = context.GetParent()->CreateBuffer(BufferDesc::CreateBLAS(pPostCompactSizes->CompactedSizeInBytes), "BLAS.Compacted");
			context.GetCommandList()->CopyRaytracingAccelerationStructure(pTargetBLAS->GetGpuHandle(), (*pSourceBLAS)->GetGpuHandle(), D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_COMPACT);
			*pSourceBLAS = pTargetBLAS;
			pPostCompactSizes++;
		}
		//E_LOG(Info, "Compacted %d BLAS instances", m_ActiveRequests.size());
		m_ActiveRequests.clear();
	}

	for (Ref<Buffer>* pSourceBLAS : m_QueuedRequests)
	{
		m_ActiveRequests.push_back(pSourceBLAS);
		if (m_ActiveRequests.size() >= sMaxNumCompactionsPerFrame)
		{
			break;
		}
	}

	if(!m_ActiveRequests.empty())
	{
		m_QueuedRequests.erase(m_QueuedRequests.begin(), m_QueuedRequests.begin() + m_ActiveRequests.size());

		if (!m_pPostBuildInfoBuffer)
		{
			uint32 requiredSize = sMaxNumCompactionsPerFrame * sizeof(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE_DESC);
			m_pPostBuildInfoBuffer = context.GetParent()->CreateBuffer(BufferDesc{ .Size = requiredSize, .Flags = BufferFlag::UnorderedAccess }, "BLASCompaction.PostBuildInfo");
			m_pPostBuildInfoReadbackBuffer = context.GetParent()->CreateBuffer(BufferDesc::CreateReadback(requiredSize), "BLASCompaction.PostBuildInfoReadback");
		}

		Array<D3D12_GPU_VIRTUAL_ADDRESS> blasAddresses;
		blasAddresses.reserve(m_ActiveRequests.size());
		for (Ref<Buffer>* pSourceBLAS : m_ActiveRequests)
			blasAddresses.push_back((*pSourceBLAS)->GetGpuHandle());

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC desc;
		desc.DestBuffer = m_pPostBuildInfoBuffer->GetGpuHandle();
		desc.InfoType = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE;

		// UAV barrier to ensure BLAS creation is finished
		context.InsertUAVBarrier();
		context.InsertResourceBarrier(m_pPostBuildInfoBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		context.FlushResourceBarriers();
		context.GetCommandList()->EmitRaytracingAccelerationStructurePostbuildInfo(&desc, (uint32)blasAddresses.size(), blasAddresses.data());

		context.InsertResourceBarrier(m_pPostBuildInfoBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
		context.CopyResource(m_pPostBuildInfoBuffer, m_pPostBuildInfoReadbackBuffer);

		m_PostBuildInfoFence = SyncPoint(context.GetParent()->GetFrameFence(), context.GetParent()->GetFrameFence()->GetCurrentValue());
	}
}

