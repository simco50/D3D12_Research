#include "stdafx.h"
#include "AccelerationStructure.h"
#include "RHI/Graphics.h"
#include "RHI/CommandContext.h"
#include "RHI/Buffer.h"
#include "RHI/DynamicResourceAllocator.h"
#include "SceneView.h"
#include "Mesh.h"
#include "Profiler.h"
#include "Core/ConsoleVariables.h"

namespace Tweakables
{
	static const uint32 gMaxNumBLASVerticesPerFrame = 100'000;
	static const uint32 gMaxNumCompactionsPerFrame = 32;

	extern ConsoleVariable<float> g_TLASBoundsThreshold;
}

void AccelerationStructure::Build(CommandContext& context, const SceneView& view)
{
	GraphicsDevice* pDevice = context.GetParent();
	if (pDevice->GetCapabilities().SupportsRaytracing())
	{
		GPU_PROFILE_SCOPE("Build Acceleration Structures", &context);

		ID3D12GraphicsCommandList4* pCmd = context.GetRaytracingCommandList();

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
		std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescs;

		uint32 numBLASBuiltVertices = 0;
		uint32 numBuiltBLAS = 0;

		for (const Batch& batch : view.Batches)
		{
			SubMesh* pMesh = batch.pMesh;
			Mesh* pParentMesh = pMesh->pParent;

			if (!pMesh->pBLAS && numBLASBuiltVertices < Tweakables::gMaxNumBLASVerticesPerFrame)
			{
				numBLASBuiltVertices += pMesh->PositionStreamLocation.Elements;
				++numBuiltBLAS;

				const Material& material = pParentMesh->GetMaterial(pMesh->MaterialId);
				D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc{};
				geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
				geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
				if (material.AlphaMode == MaterialAlphaMode::Opaque)
				{
					geometryDesc.Flags |= D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
				}
				geometryDesc.Triangles.IndexBuffer = pMesh->IndicesLocation.Location;
				geometryDesc.Triangles.IndexCount = pMesh->IndicesLocation.Elements;
				geometryDesc.Triangles.IndexFormat = D3D::ConvertFormat(pMesh->IndicesLocation.Format);
				geometryDesc.Triangles.Transform3x4 = 0;
				geometryDesc.Triangles.VertexBuffer.StartAddress = pMesh->PositionStreamLocation.Location;
				geometryDesc.Triangles.VertexBuffer.StrideInBytes = pMesh->PositionStreamLocation.Stride;
				geometryDesc.Triangles.VertexCount = pMesh->PositionStreamLocation.Elements;
				geometryDesc.Triangles.VertexFormat = D3D::ConvertFormat(pMesh->PositionsFormat);

				D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS prebuildInfo{};
				prebuildInfo.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
				prebuildInfo.Flags =
					D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE
					| D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION;
				prebuildInfo.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
				prebuildInfo.NumDescs = 1;
				prebuildInfo.pGeometryDescs = &geometryDesc;

				D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
				pDevice->GetRaytracingDevice()->GetRaytracingAccelerationStructurePrebuildInfo(&prebuildInfo, &info);

				RefCountPtr<Buffer> pBLASScratch = pDevice->CreateBuffer(BufferDesc::CreateByteAddress(Math::AlignUp<uint64>(info.ScratchDataSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT), BufferFlag::UnorderedAccess | BufferFlag::NoBindless), "BLAS.ScratchBuffer");
				RefCountPtr<Buffer> pBLAS = pDevice->CreateBuffer(BufferDesc::CreateBLAS(Math::AlignUp<uint64>(info.ResultDataMaxSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT)), "BLAS.Buffer");

				D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc{};
				asDesc.Inputs = prebuildInfo;
				asDesc.DestAccelerationStructureData = pBLAS->GetGpuHandle();
				asDesc.ScratchAccelerationStructureData = pBLASScratch->GetGpuHandle();
				asDesc.SourceAccelerationStructureData = 0;

				pCmd->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);
				context.InsertUavBarrier(pMesh->pBLAS);

				pMesh->pBLAS = pBLAS.Detach();

				m_QueuedRequests.push_back({ &pMesh->pBLAS });
			}

			if (pMesh->pBLAS)
			{
				//if (m_RenderPath != RenderPath::PathTracing)
				{
					// Cull object that are small to the viewer - Deligiannis2019
					Vector3 cameraVec = (batch.Bounds.Center - view.View.Position);
					float angle = tanf(batch.Radius / cameraVec.Length());
					if (angle < Tweakables::g_TLASBoundsThreshold && cameraVec.Length() > batch.Radius)
					{
						continue;
					}
				}

				D3D12_RAYTRACING_INSTANCE_DESC instanceDesc{};
				instanceDesc.AccelerationStructure = pMesh->pBLAS->GetGpuHandle();
				instanceDesc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
				instanceDesc.InstanceContributionToHitGroupIndex = 0;
				instanceDesc.InstanceID = batch.InstanceData.World;
				instanceDesc.InstanceMask = 0xFF;

				// Hack
				if (batch.WorldMatrix.Determinant() < 0)
				{
					instanceDesc.Flags |= D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE;
				}

				//The layout of Transform is a transpose of how affine matrices are typically stored in memory. Instead of four 3-vectors, Transform is laid out as three 4-vectors.
				auto ApplyTransform = [](const Matrix& m, D3D12_RAYTRACING_INSTANCE_DESC& desc)
				{
					Matrix transpose = m.Transpose();
					memcpy(&desc.Transform, &transpose, sizeof(float) * 12);
				};

				ApplyTransform(batch.WorldMatrix, instanceDesc);
				instanceDescs.push_back(instanceDesc);
			}
		}

		if (numBuiltBLAS > 0)
		{
			//E_LOG(Info, "Built %d BLAS instances. %d vertices", numBuiltBLAS, numBLASBuiltVertices);
		}

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS prebuildInfo{};
		prebuildInfo.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
		prebuildInfo.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		prebuildInfo.Flags = buildFlags;
		prebuildInfo.NumDescs = (uint32)instanceDescs.size();

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
		pDevice->GetRaytracingDevice()->GetRaytracingAccelerationStructurePrebuildInfo(&prebuildInfo, &info);

		if (!m_pTLAS || m_pTLAS->GetSize() < info.ResultDataMaxSizeInBytes)
		{
			m_pScratch = pDevice->CreateBuffer(BufferDesc::CreateByteAddress(Math::AlignUp<uint64>(info.ScratchDataSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT)), "TLAS.ScratchBuffer");
			m_pTLAS = pDevice->CreateBuffer(BufferDesc::CreateTLAS(Math::AlignUp<uint64>(info.ResultDataMaxSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT)), "TLAS.Buffer");
		}

		DynamicAllocation allocation = context.AllocateTransientMemory(instanceDescs.size() * sizeof(D3D12_RAYTRACING_INSTANCE_DESC));
		memcpy(allocation.pMappedMemory, instanceDescs.data(), instanceDescs.size() * sizeof(D3D12_RAYTRACING_INSTANCE_DESC));

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc{};
		asDesc.DestAccelerationStructureData = m_pTLAS->GetGpuHandle();
		asDesc.ScratchAccelerationStructureData = m_pScratch->GetGpuHandle();
		asDesc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
		asDesc.Inputs.Flags = buildFlags;
		asDesc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		asDesc.Inputs.InstanceDescs = allocation.GpuHandle;
		asDesc.Inputs.NumDescs = (uint32)instanceDescs.size();
		asDesc.SourceAccelerationStructureData = 0;

		pCmd->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);
		context.InsertUavBarrier(m_pTLAS);

		ProcessCompaction(context);
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
	GPU_PROFILE_SCOPE("BLAS Compaction", &context);

	if (!m_ActiveRequests.empty())
	{
		if (!m_PostBuildInfoFence.IsComplete())
		{
			return;
		}

		const uint64* pPostCompactSizes = static_cast<uint64*>(m_pPostBuildInfoReadbackBuffer->GetMappedData());
		for (const CompactionRequest& request : m_ActiveRequests)
		{
			uint64 size = *pPostCompactSizes++;
			Buffer* pSourceBLAS = *request.pBLAS;
			RefCountPtr<Buffer> pTargetBLAS = context.GetParent()->CreateBuffer(BufferDesc::CreateBLAS(size), "BLAS.Compacted");
			context.GetRaytracingCommandList()->CopyRaytracingAccelerationStructure(pTargetBLAS->GetGpuHandle(), pSourceBLAS->GetGpuHandle(), D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_COMPACT);
			delete* request.pBLAS;
			*request.pBLAS = pTargetBLAS.Detach();
		}
		//E_LOG(Info, "Compacted %d BLAS instances", m_ActiveRequests.size());
		m_ActiveRequests.clear();
	}

	for (CompactionRequest& request : m_QueuedRequests)
	{
		m_ActiveRequests.push_back({ request.pBLAS });
		if (m_ActiveRequests.size() >= Tweakables::gMaxNumCompactionsPerFrame)
		{
			break;
		}
	}

	if(!m_ActiveRequests.empty())
	{
		m_QueuedRequests.erase(m_QueuedRequests.begin(), m_QueuedRequests.begin() + m_ActiveRequests.size());

		uint32 requiredSize = Tweakables::gMaxNumCompactionsPerFrame * sizeof(uint64);
		if (!m_pPostBuildInfoBuffer)
		{
			m_pPostBuildInfoBuffer = context.GetParent()->CreateBuffer(BufferDesc::CreateByteAddress(requiredSize), "BLASCompaction.PostBuildInfo");
			m_pPostBuildInfoReadbackBuffer = context.GetParent()->CreateBuffer(BufferDesc::CreateReadback(requiredSize), "BLASCompaction.PostBuildInfoReadback");
		}

		std::vector<D3D12_GPU_VIRTUAL_ADDRESS> blasAddresses;
		blasAddresses.reserve(m_ActiveRequests.size());
		for (CompactionRequest& request : m_ActiveRequests)
		{
			blasAddresses.push_back((*request.pBLAS)->GetGpuHandle());
		}
		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC desc;
		desc.DestBuffer = m_pPostBuildInfoBuffer->GetGpuHandle();
		desc.InfoType = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE;

		context.InsertResourceBarrier(m_pPostBuildInfoBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		context.FlushResourceBarriers();

		context.GetRaytracingCommandList()->EmitRaytracingAccelerationStructurePostbuildInfo(&desc, (uint32)blasAddresses.size(), blasAddresses.data());

		context.InsertUavBarrier(m_pPostBuildInfoBuffer);
		context.InsertResourceBarrier(m_pPostBuildInfoBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE);
		context.FlushResourceBarriers();

		context.CopyResource(m_pPostBuildInfoBuffer, m_pPostBuildInfoReadbackBuffer);

		m_PostBuildInfoFence = SyncPoint(context.GetParent()->GetFrameFence(), context.GetParent()->GetFrameFence()->GetCurrentValue());
	}
}

