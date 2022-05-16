#include "stdafx.h"
#include "AccelerationStructure.h"
#include "RHI/Graphics.h"
#include "RHI/CommandContext.h"
#include "RHI/Buffer.h"
#include "RHI/DynamicResourceAllocator.h"
#include "Mesh.h"

void AccelerationStructure::AddInstance(uint32 ID, SubMesh* pMesh, const Matrix& transform)
{
	m_Instances.push_back(Instance{ ID, pMesh, transform });
}

void AccelerationStructure::Build(CommandContext& context)
{
	GraphicsDevice* pDevice = context.GetParent();
	if (pDevice->GetCapabilities().SupportsRaytracing())
	{
		ID3D12GraphicsCommandList4* pCmd = context.GetRaytracingCommandList();

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
		std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescs;

		for (const Instance& instance : m_Instances)
		{
			SubMesh* pMesh = instance.pMesh;
			Mesh* pParentMesh = pMesh->pParent;
			if (!pMesh->pBLAS)
			{
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
				geometryDesc.Triangles.IndexFormat = pMesh->IndicesLocation.Format;
				geometryDesc.Triangles.Transform3x4 = 0;
				geometryDesc.Triangles.VertexBuffer.StartAddress = pMesh->PositionStreamLocation.Location;
				geometryDesc.Triangles.VertexBuffer.StrideInBytes = pMesh->PositionStreamLocation.Stride;
				geometryDesc.Triangles.VertexCount = pMesh->PositionStreamLocation.Elements;
				geometryDesc.Triangles.VertexFormat = pMesh->PositionsFormat;

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

				RefCountPtr<Buffer> pBLASScratch = pDevice->CreateBuffer(BufferDesc::CreateByteAddress(Math::AlignUp<uint64>(info.ScratchDataSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT), BufferFlag::UnorderedAccess | BufferFlag::NoBindless), "BLAS Scratch Buffer");
				RefCountPtr<Buffer> pBLAS = pDevice->CreateBuffer(BufferDesc::CreateByteAddress(Math::AlignUp<uint64>(info.ResultDataMaxSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT), BufferFlag::UnorderedAccess | BufferFlag::AccelerationStructure | BufferFlag::NoBindless), "BLAS Buffer");

				D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc{};
				asDesc.Inputs = prebuildInfo;
				asDesc.DestAccelerationStructureData = pBLAS->GetGpuHandle();
				asDesc.ScratchAccelerationStructureData = pBLASScratch->GetGpuHandle();
				asDesc.SourceAccelerationStructureData = 0;

				pCmd->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);
				context.InsertUavBarrier(pMesh->pBLAS);

				pMesh->pBLAS = pBLAS.Detach();
				pMesh->pBLASScratch = pBLASScratch.Detach();
			}

#if 0
			if (m_RenderPath != RenderPath::PathTracing)
			{
				// Cull object that are small to the viewer - Deligiannis2019
				Vector3 cameraVec = (batch.Bounds.Center - view.View.Position);
				float angle = tanf(batch.Radius / cameraVec.Length());
				if (angle < Tweakables::g_TLASBoundsThreshold && cameraVec.Length() > batch.Radius)
				{
					continue;
				}
			}
#endif

			D3D12_RAYTRACING_INSTANCE_DESC instanceDesc{};
			instanceDesc.AccelerationStructure = pMesh->pBLAS->GetGpuHandle();
			instanceDesc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
			instanceDesc.InstanceContributionToHitGroupIndex = 0;
			instanceDesc.InstanceID = instance.ID;
			instanceDesc.InstanceMask = 0xFF;

			// Hack
			if (instance.Transform.Determinant() < 0)
			{
				instanceDesc.Flags |= D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE;
			}

			//The layout of Transform is a transpose of how affine matrices are typically stored in memory. Instead of four 3-vectors, Transform is laid out as three 4-vectors.
			auto ApplyTransform = [](const Matrix& m, D3D12_RAYTRACING_INSTANCE_DESC& desc)
			{
				Matrix transpose = m.Transpose();
				memcpy(&desc.Transform, &transpose, sizeof(float) * 12);
			};

			ApplyTransform(instance.Transform, instanceDesc);
			instanceDescs.push_back(instanceDesc);
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
			m_pScratch = pDevice->CreateBuffer(BufferDesc::CreateByteAddress(Math::AlignUp<uint64>(info.ScratchDataSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT)), "TLAS Scratch");
			m_pTLAS = pDevice->CreateBuffer(BufferDesc::CreateAccelerationStructure(Math::AlignUp<uint64>(info.ResultDataMaxSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT)), "TLAS");
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

void AccelerationStructure::Reset()
{
	m_Instances.clear();
}
